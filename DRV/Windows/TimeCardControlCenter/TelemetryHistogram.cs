using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;

namespace TimeCardControlCenter
{
    public sealed class TelemetryHistogram : FrameworkElement
    {
        public static readonly DependencyProperty BarBrushProperty =
            DependencyProperty.Register("BarBrush", typeof(Brush),
                typeof(TelemetryHistogram), new FrameworkPropertyMetadata(
                    Brushes.DeepSkyBlue,
                    FrameworkPropertyMetadataOptions.AffectsRender));

        public static readonly DependencyProperty FillBrushProperty =
            DependencyProperty.Register("FillBrush", typeof(Brush),
                typeof(TelemetryHistogram), new FrameworkPropertyMetadata(
                    Brushes.Transparent,
                    FrameworkPropertyMetadataOptions.AffectsRender));

        private readonly List<double> samples = new List<double>();
        private int capacity = 1800;
        private int binCount = 28;
        private int hotBin = -1;

        public TelemetryHistogram()
        {
            Focusable = true;
            SnapsToDevicePixels = true;
            MouseMove += Histogram_MouseMove;
            MouseLeave += delegate
            {
                hotBin = -1;
                ToolTip = null;
                InvalidateVisual();
            };
        }

        public Brush BarBrush
        {
            get { return (Brush)GetValue(BarBrushProperty); }
            set { SetValue(BarBrushProperty, value); }
        }

        public Brush FillBrush
        {
            get { return (Brush)GetValue(FillBrushProperty); }
            set { SetValue(FillBrushProperty, value); }
        }

        public int Capacity
        {
            get { return capacity; }
            set
            {
                capacity = Math.Max(30, value);
                Trim();
                InvalidateVisual();
            }
        }

        public int BinCount
        {
            get { return binCount; }
            set
            {
                binCount = Math.Max(8, Math.Min(80, value));
                InvalidateVisual();
            }
        }

        public int SampleCount { get { return samples.Count; } }

        public double Median { get { return Percentile(.50); } }
        public double Percentile95 { get { return Percentile(.95); } }
        public double Percentile99 { get { return Percentile(.99); } }

        public void AddSample(double value)
        {
            if (double.IsNaN(value) || double.IsInfinity(value))
                return;
            samples.Add(value);
            Trim();
            InvalidateVisual();
        }

        public void Clear()
        {
            samples.Clear();
            hotBin = -1;
            InvalidateVisual();
        }

        public string Summary(string unit)
        {
            if (samples.Count == 0)
                return "Waiting for samples";
            return string.Format(CultureInfo.InvariantCulture,
                "n={0} · median {1:F0} {4} · p95 {2:F0} {4} · p99 {3:F0} {4}",
                samples.Count, Median, Percentile95, Percentile99, unit);
        }

        protected override void OnRender(DrawingContext dc)
        {
            base.OnRender(dc);
            if (ActualWidth < 20 || ActualHeight < 20)
                return;
            Rect plot = new Rect(.5, .5, Math.Max(1, ActualWidth - 1),
                Math.Max(1, ActualHeight - 1));
            Pen gridPen = new Pen(new SolidColorBrush(
                Color.FromArgb(38, 143, 166, 187)), 1);
            gridPen.Freeze();
            for (int index = 0; index <= 4; index++)
            {
                double y = plot.Top + plot.Height * index / 4.0;
                dc.DrawLine(gridPen, new Point(plot.Left, y),
                    new Point(plot.Right, y));
            }
            if (samples.Count == 0)
                return;

            double minimum;
            double maximum;
            int[] bins = BuildBins(out minimum, out maximum);
            int maximumCount = Math.Max(1, bins.Max());
            double slotWidth = plot.Width / bins.Length;
            for (int index = 0; index < bins.Length; index++)
            {
                double height = plot.Height * bins[index] / maximumCount;
                Rect bar = new Rect(plot.Left + index * slotWidth + 1,
                    plot.Bottom - height, Math.Max(1, slotWidth - 2), height);
                Brush fill = index == hotBin ? BarBrush : FillBrush;
                Pen outline = new Pen(BarBrush, index == hotBin ? 1.8 : 1.0);
                dc.DrawRoundedRectangle(fill, outline, bar, 2, 2);
            }

            DrawPercentile(dc, plot, minimum, maximum, Median,
                Color.FromRgb(105, 196, 65));
            DrawPercentile(dc, plot, minimum, maximum, Percentile95,
                Color.FromRgb(218, 181, 57));
        }

        private void DrawPercentile(DrawingContext dc, Rect plot,
                                    double minimum, double maximum,
                                    double value, Color color)
        {
            double normalized = maximum == minimum ? .5 :
                (value - minimum) / (maximum - minimum);
            double x = plot.Left + normalized * plot.Width;
            Pen pen = new Pen(new SolidColorBrush(color), 1.2)
            {
                DashStyle = new DashStyle(new[] { 3.0, 3.0 }, 0)
            };
            dc.DrawLine(pen, new Point(x, plot.Top), new Point(x, plot.Bottom));
        }

        private int[] BuildBins(out double minimum, out double maximum)
        {
            minimum = samples.Min();
            maximum = samples.Max();
            if (maximum <= minimum)
            {
                double padding = Math.Max(1, Math.Abs(minimum) * .02);
                minimum -= padding;
                maximum += padding;
            }
            else
            {
                double padding = (maximum - minimum) * .04;
                minimum -= padding;
                maximum += padding;
            }
            int[] bins = new int[binCount];
            foreach (double value in samples)
            {
                int index = (int)((value - minimum) /
                    (maximum - minimum) * bins.Length);
                bins[Math.Max(0, Math.Min(bins.Length - 1, index))]++;
            }
            return bins;
        }

        private double Percentile(double fraction)
        {
            if (samples.Count == 0)
                return 0;
            double[] sorted = samples.OrderBy(value => value).ToArray();
            double position = (sorted.Length - 1) * fraction;
            int lower = (int)Math.Floor(position);
            int upper = (int)Math.Ceiling(position);
            if (lower == upper)
                return sorted[lower];
            return sorted[lower] + (sorted[upper] - sorted[lower]) *
                (position - lower);
        }

        private void Histogram_MouseMove(object sender, MouseEventArgs e)
        {
            if (samples.Count == 0 || ActualWidth <= 0)
                return;
            double minimum;
            double maximum;
            int[] bins = BuildBins(out minimum, out maximum);
            hotBin = Math.Max(0, Math.Min(bins.Length - 1,
                (int)(e.GetPosition(this).X / ActualWidth * bins.Length)));
            double start = minimum + (maximum - minimum) * hotBin / bins.Length;
            double end = minimum + (maximum - minimum) * (hotBin + 1) / bins.Length;
            ToolTip = string.Format(CultureInfo.InvariantCulture,
                "{0:F0} to {1:F0} ns\n{2} sample(s)", start, end, bins[hotBin]);
            InvalidateVisual();
        }

        private void Trim()
        {
            while (samples.Count > capacity)
                samples.RemoveAt(0);
        }
    }
}
