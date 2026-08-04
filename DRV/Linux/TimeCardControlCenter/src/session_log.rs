use std::collections::VecDeque;
use std::fs::{self, OpenOptions};
use std::io::{self, Write};
use std::path::Path;

use chrono::{DateTime, SecondsFormat, Utc};
use serde::Serialize;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "UPPERCASE")]
pub enum SessionLogSeverity {
    Trace,
    Info,
    Warn,
    Error,
}

impl SessionLogSeverity {
    #[must_use]
    pub const fn label(self) -> &'static str {
        match self {
            Self::Trace => "TRACE",
            Self::Info => "INFO",
            Self::Warn => "WARN",
            Self::Error => "ERROR",
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SessionLogRecord {
    pub timestamp_utc: DateTime<Utc>,
    pub severity: SessionLogSeverity,
    pub category: String,
    pub card_context: String,
    pub message: String,
}

impl SessionLogRecord {
    #[must_use]
    pub fn to_text(&self) -> String {
        format!(
            "{} [{}] [{}] [{}] {}",
            self.timestamp_utc
                .to_rfc3339_opts(SecondsFormat::Millis, true),
            self.severity.label(),
            self.category,
            self.card_context,
            self.message
        )
    }
}

#[derive(Debug)]
pub struct SessionLogStore {
    capacity: usize,
    records: VecDeque<SessionLogRecord>,
    dropped_record_count: u64,
}

impl SessionLogStore {
    /// Creates a bounded session log.
    ///
    /// # Panics
    ///
    /// Panics when `capacity` is zero.
    #[must_use]
    pub fn new(capacity: usize) -> Self {
        assert!(capacity > 0, "session log capacity must be positive");
        Self {
            capacity,
            records: VecDeque::with_capacity(capacity),
            dropped_record_count: 0,
        }
    }

    #[must_use]
    pub const fn capacity(&self) -> usize {
        self.capacity
    }

    #[must_use]
    pub fn len(&self) -> usize {
        self.records.len()
    }

    #[must_use]
    pub fn is_empty(&self) -> bool {
        self.records.is_empty()
    }

    #[must_use]
    pub const fn dropped_record_count(&self) -> u64 {
        self.dropped_record_count
    }

    pub fn append(
        &mut self,
        severity: SessionLogSeverity,
        category: &str,
        card_context: &str,
        message: &str,
    ) {
        self.append_at(severity, category, card_context, message, Utc::now());
    }

    pub fn append_at(
        &mut self,
        severity: SessionLogSeverity,
        category: &str,
        card_context: &str,
        message: &str,
        timestamp_utc: DateTime<Utc>,
    ) {
        if self.records.len() == self.capacity {
            self.records.pop_front();
            self.dropped_record_count += 1;
        }
        self.records.push_back(SessionLogRecord {
            timestamp_utc,
            severity,
            category: clean_field(category, "Application"),
            card_context: clean_field(card_context, "offline"),
            message: clean_field(message, "(empty message)"),
        });
    }

    pub fn clear(&mut self) {
        self.records.clear();
        self.dropped_record_count = 0;
    }

    #[must_use]
    pub fn text_lines(&self, maximum: Option<usize>) -> Vec<String> {
        let skip = maximum.map_or(0, |maximum| self.records.len().saturating_sub(maximum));
        self.records
            .iter()
            .skip(skip)
            .map(SessionLogRecord::to_text)
            .collect()
    }

    /// Serializes the retained records using the stable version 1 export schema.
    ///
    /// # Errors
    ///
    /// Returns a serialization error if a record cannot be encoded.
    pub fn to_json(&self) -> Result<Vec<u8>, serde_json::Error> {
        #[derive(Serialize)]
        #[serde(rename_all = "camelCase")]
        struct Export<'a> {
            schema_version: u8,
            exported_at_utc: DateTime<Utc>,
            capacity: usize,
            retained_record_count: usize,
            dropped_record_count: u64,
            records: &'a VecDeque<SessionLogRecord>,
        }

        serde_json::to_vec_pretty(&Export {
            schema_version: 1,
            exported_at_utc: Utc::now(),
            capacity: self.capacity,
            retained_record_count: self.records.len(),
            dropped_record_count: self.dropped_record_count,
            records: &self.records,
        })
    }

    /// Writes an export through a same-directory temporary file, then renames it.
    ///
    /// # Errors
    ///
    /// Returns an I/O or JSON serialization error when the export cannot be committed.
    pub fn write_json(&self, path: &Path) -> Result<(), SessionLogWriteError> {
        let contents = self.to_json()?;
        let parent = path.parent().unwrap_or_else(|| Path::new("."));
        fs::create_dir_all(parent)?;
        let file_name = path
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or("timecard-session.json");
        let temporary = parent.join(format!(".{file_name}.{}.tmp", std::process::id()));
        let mut file = OpenOptions::new()
            .create(true)
            .truncate(true)
            .write(true)
            .open(&temporary)?;
        file.write_all(&contents)?;
        file.sync_all()?;
        fs::rename(&temporary, path).inspect_err(|_| {
            let _ = fs::remove_file(&temporary);
        })?;
        Ok(())
    }
}

impl Default for SessionLogStore {
    fn default() -> Self {
        Self::new(500)
    }
}

#[derive(Debug, thiserror::Error)]
pub enum SessionLogWriteError {
    #[error("could not serialize the session log: {0}")]
    Json(#[from] serde_json::Error),
    #[error("could not write the session log: {0}")]
    Io(#[from] io::Error),
}

fn clean_field(value: &str, fallback: &str) -> String {
    let cleaned = value
        .trim()
        .replace("\r\n", " | ")
        .replace(['\r', '\n'], " ");
    if cleaned.is_empty() {
        fallback.to_owned()
    } else {
        cleaned
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bounded_log_discards_oldest_records() {
        let mut log = SessionLogStore::new(2);
        log.append(SessionLogSeverity::Info, "test", "ocp0", "one");
        log.append(SessionLogSeverity::Warn, "test", "ocp0", "two");
        log.append(SessionLogSeverity::Error, "test", "ocp0", "three");
        assert_eq!(log.len(), 2);
        assert_eq!(log.dropped_record_count(), 1);
        assert!(!log.text_lines(None)[0].contains("one"));
    }

    #[test]
    fn log_fields_are_single_line() {
        let mut log = SessionLogStore::new(1);
        log.append(SessionLogSeverity::Info, "", "", "line 1\nline 2");
        let line = &log.text_lines(None)[0];
        assert!(line.contains("[Application] [offline] line 1 line 2"));
    }

    #[test]
    fn json_export_has_stable_schema() {
        let mut log = SessionLogStore::new(2);
        log.append(SessionLogSeverity::Info, "Telemetry", "ocp0", "sampled");
        let value: serde_json::Value = serde_json::from_slice(&log.to_json().unwrap()).unwrap();
        assert_eq!(value["schemaVersion"], 1);
        assert_eq!(value["retainedRecordCount"], 1);
        assert_eq!(value["records"][0]["cardContext"], "ocp0");
    }
}
