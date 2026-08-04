use crate::TimeCardSnapshot;

/// Read-only hardware boundary used by the desktop process.
pub trait TimeCardBackend: Send {
    fn backend_name(&self) -> &'static str;
    fn selected_device(&self) -> &str;
    fn set_selected_device(&mut self, device_id: &str);
    fn available_devices(&self) -> Vec<String>;
    fn read_snapshot(&mut self) -> TimeCardSnapshot;
}
