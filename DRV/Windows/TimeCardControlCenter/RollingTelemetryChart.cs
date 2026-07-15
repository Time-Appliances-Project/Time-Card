using System;
using System.Collections.Generic;
using System.Windows;
using System.Windows.Media;

namespace TimeCardControlCenter
{
    public sealed class RollingTelemetryChart : FrameworkElement
    {
        public static readonly DependencyProperty LineBrushProperty = DependencyProperty.Register(
            "LineBrush", typeof(Brush), typeof(RollingTelemetryChart),
            new FrameworkPropertyMetadata(Brushes.DeepSkyBlue,
                FrameworkPropertyMetadataOptions.AffectsRender));

        public static readonly DependencyProperty AreaBrushProperty = DependencyProperty.Register(
            "AreaBrush", typeof(Brush), typeof(RollingTelemetryChart),
            new FrameworkPropertyMetadata(Brushes.Transparent,
                FrameworkPropertyMetadataOptions.AffectsRender));

        public static readonly DependencyProperty CenterZeroProperty = DependencyProperty.Register(
            "CenterZero", typeof(bool), typeof(RollingTelemetryChart),
            new FrameworkPropertyMetadata(false,
                FrameworkPropertyMetadataOptions.AffectsRender));

        public static readonly DependencyProperty MinimumCenteredExtentProperty =
            DependencyProperty.Register(
                "MinimumCenteredExtent", typeof(double),
                typeof(RollingTelemetryChart),
                new FrameworkPropertyMetadata(0.0,
                    FrameworkPropertyMetadataOptions.AffectsRender));

        public static readonly DependencyProperty MinimumRangeProperty =
            DependencyProperty.Register(
                "MinimumRange", typeof(double),
                typeof(RollingTelemetryChart),
                new FrameworkPropertyMetadata(0.0,
                    FrameworkPropertyMetadataOptions.AffectsRender));

        private readonly List<double> samples = new List<double>();
        private int capacity = 60;

        public RollingTelemetryChart()
        {
            SnapsToDevicePixels = true;
            Focusable = false;
        }

        public Brush LineBrush
        {
            get { return (Brush)GetValue(LineBrushProperty); }
            set { SetValue(LineBrushProperty, value); }
        }

        public Brush AreaBrush
        {
            get { return (Brush)GetValue(AreaBrushProperty); }
            set { SetValue(AreaBrushProperty, value); }
        }

        public bool CenterZero
        {
            get { return (bool)GetValue(CenterZeroProperty); }
            set { SetValue(CenterZeroProperty, value); }
        }

        public double MinimumCenteredExtent
        {
            get { return (double)GetValue(MinimumCenteredExtentProperty); }
            set { SetValue(MinimumCenteredExtentProperty, value); }
        }

        public double MinimumRange
        {
            get { return (double)GetValue(MinimumRangeProperty); }
            set { SetValue(MinimumRangeProperty, value); }
        }

        public int Capacity
        {
            get { return capacity; }
            set
            {
                capacity = Math.Max(2, value);
                TrimToCapacity();
                InvalidateVisual();
            }
        }

        public int SampleCount { get { return samples.Count; } }

        public double Minimum
        {
            get
            {
                if (samples.Count == 0)
                    return 0;
                double value = samples[0];
                for (int index = 1; index < samples.Count; index++)
                    value = Math.Min(value, samples[index]);
                return value;
            }
        }

        public double Maximum
        {
            get
            {
                if (samples.Count == 0)
                    return 0;
                double value = samples[0];
                for (int index = 1; index < samples.Count; index++)
                    value = Math.Max(value, samples[index]);
                return value;
            }
        }

        public double VisibleMinimum
        {
            get
            {
                double minimum;
                double maximum;
                GetScaleBounds(out minimum, out maximum);
                return minimum;
            }
        }

        public double VisibleMaximum
        {
            get
            {
                double minimum;
                double maximum;
                GetScaleBounds(out minimum, out maximum);
                return maximum;
            }
        }

        public void AddSample(double value)
        {
            if (double.IsNaN(value) || double.IsInfinity(value))
                return;
            samples.Add(value);
            TrimToCapacity();
            InvalidateVisual();
        }

        public void Clear()
        {
            samples.Clear();
            InvalidateVisual();
        }

        protected override void OnRender(DrawingContext drawingContext)
        {
            base.OnRender(drawingContext);
            if (ActualWidth < 2 || ActualHeight < 2)
                return;

            Rect plot = new Rect(0.5, 0.5,
                Math.Max(1, ActualWidth - 1), Math.Max(1, ActualHeight - 1));
            Pen gridPen = new Pen(new SolidColorBrush(Color.FromArgb(38, 143, 166, 187)), 1);
            gridPen.Freeze();

            for (int index = 0; index <= 4; index++)
            {
                double x = plot.Left + (plot.Width * index / 4.0);
                double y = plot.Top + (plot.Height * index / 4.0);
                drawingContext.DrawLine(gridPen, new Point(x, plot.Top), new Point(x, plot.Bottom));
                drawingContext.DrawLine(gridPen, new Point(plot.Left, y), new Point(plot.Right, y));
            }

            if (samples.Count == 0)
                return;

            double minimum;
            double maximum;
            GetScaleBounds(out minimum, out maximum);

            if (minimum < 0 && maximum > 0)
            {
                double zeroY = ValueToY(0, minimum, maximum, plot);
                Pen zeroPen = new Pen(new SolidColorBrush(Color.FromArgb(82, 76, 201, 240)), 1);
                zeroPen.Freeze();
                drawingContext.DrawLine(zeroPen,
                    new Point(plot.Left, zeroY), new Point(plot.Right, zeroY));
            }

            Point[] points = new Point[samples.Count];
            for (int index = 0; index < samples.Count; index++)
            {
                double x = samples.Count == 1 ? plot.Right :
                    plot.Left + (plot.Width * index / (samples.Count - 1.0));
                points[index] = new Point(x,
                    ValueToY(samples[index], minimum, maximum, plot));
            }

            drawingContext.PushClip(new RectangleGeometry(plot));

            StreamGeometry area = new StreamGeometry();
            using (StreamGeometryContext context = area.Open())
            {
                context.BeginFigure(new Point(points[0].X, plot.Bottom), true, true);
                context.LineTo(points[0], true, false);
                for (int index = 1; index < points.Length; index++)
                    context.LineTo(points[index], true, false);
                context.LineTo(new Point(points[points.Length - 1].X, plot.Bottom), true, false);
            }
            area.Freeze();
            drawingContext.DrawGeometry(AreaBrush, null, area);

            if (points.Length > 1)
            {
                StreamGeometry line = new StreamGeometry();
                using (StreamGeometryContext context = line.Open())
                {
                    context.BeginFigure(points[0], false, false);
                    for (int index = 1; index < points.Length; index++)
                        context.LineTo(points[index], true, false);
                }
                line.Freeze();
                Pen linePen = new Pen(LineBrush, 2.15)
                {
                    StartLineCap = PenLineCap.Round,
                    EndLineCap = PenLineCap.Round,
                    LineJoin = PenLineJoin.Round
                };
                drawingContext.DrawGeometry(null, linePen, line);
            }

            Point latest = points[points.Length - 1];
            drawingContext.DrawEllipse(new SolidColorBrush(Color.FromArgb(44, 255, 255, 255)),
                null, latest, 5.5, 5.5);
            drawingContext.DrawEllipse(LineBrush, null, latest, 2.8, 2.8);
            drawingContext.Pop();
        }

        private void TrimToCapacity()
        {
            while (samples.Count > capacity)
                samples.RemoveAt(0);
        }

        private void GetScaleBounds(out double minimum, out double maximum)
        {
            minimum = Minimum;
            maximum = Maximum;
            if (CenterZero)
            {
                double extent = Math.Max(Math.Abs(minimum), Math.Abs(maximum));
                // Keep the largest offset comfortably inside the plot while
                // preserving a stable scale when the clock is nearly aligned.
                extent = Math.Max(extent * 1.30,
                    Math.Max(0, MinimumCenteredExtent));
                if (extent < 1)
                    extent = 1;
                minimum = -extent;
                maximum = extent;
                return;
            }

            double range = maximum - minimum;
            if (MinimumRange > 0)
            {
                /*
                 * Detail mode follows the observed band instead of forcing
                 * zero into view.  A small floor prevents integer-nanosecond
                 * noise from expanding to the full chart height.
                 */
                double center = minimum + range / 2.0;
                double visibleRange = Math.Max(range, MinimumRange);
                double detailPadding = visibleRange * 0.12;
                minimum = center - visibleRange / 2.0 - detailPadding;
                maximum = center + visibleRange / 2.0 + detailPadding;
                return;
            }
            double padding = range > 0 ? range * 0.14 :
                Math.Max(Math.Abs(maximum) * 0.08, 1);
            minimum -= padding;
            maximum += padding;
        }

        private static double ValueToY(double value, double minimum,
                                       double maximum, Rect plot)
        {
            double normalized = (value - minimum) / (maximum - minimum);
            return plot.Bottom - (normalized * plot.Height);
        }
    }
}
