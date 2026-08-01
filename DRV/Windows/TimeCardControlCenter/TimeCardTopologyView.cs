using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;

namespace TimeCardControlCenter
{
    public sealed class WorkspaceRequestEventArgs : EventArgs
    {
        public WorkspaceRequestEventArgs(string workspace) { Workspace = workspace; }
        public string Workspace { get; private set; }
    }

    public sealed class TimeCardTopologyView : FrameworkElement
    {
        private sealed class NodeLayout
        {
            public string Key;
            public string Title;
            public string Workspace;
            public Rect Bounds;
        }

        private readonly DispatcherTimer animationTimer;
        private readonly List<NodeLayout> layouts = new List<NodeLayout>();
        private HealthReport report;
        private SmaConnectorState[] smaStates = new SmaConnectorState[4];
        private double dashOffset;
        private string hotKey;

        public TimeCardTopologyView()
        {
            Cursor = Cursors.Hand;
            Focusable = true;
            SnapsToDevicePixels = true;
            animationTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(55) };
            animationTimer.Tick += delegate
            {
                dashOffset = (dashOffset + 1.3) % 24;
                InvalidateVisual();
            };
            Loaded += delegate { animationTimer.Start(); };
            Unloaded += delegate { animationTimer.Stop(); };
            MouseMove += OnMouseMove;
            MouseLeave += delegate { hotKey = null; ToolTip = null; InvalidateVisual(); };
            MouseLeftButtonUp += OnMouseLeftButtonUp;
            KeyDown += OnKeyDown;
        }

        public event EventHandler<WorkspaceRequestEventArgs> WorkspaceRequested;

        public void Update(HealthReport healthReport,
                           IEnumerable<SmaConnectorState> connectorStates)
        {
            report = healthReport;
            if (connectorStates != null)
                smaStates = connectorStates.Take(4).ToArray();
            InvalidateVisual();
        }

        protected override void OnRender(DrawingContext dc)
        {
            base.OnRender(dc);
            if (ActualWidth < 520 || ActualHeight < 190)
                return;

            Rect board = new Rect(12, 12, ActualWidth - 24, ActualHeight - 24);
            LinearGradientBrush boardBrush = new LinearGradientBrush(
                Color.FromRgb(16, 38, 61), Color.FromRgb(8, 23, 38), 0);
            dc.DrawRoundedRectangle(boardBrush,
                new Pen(new SolidColorBrush(Color.FromRgb(43, 82, 110)), 1.2),
                board, 20, 20);

            DrawBoardTraces(dc, board);
            BuildLayouts(board);
            foreach (NodeLayout layout in layouts)
                DrawNode(dc, layout);
            DrawSmaConnectors(dc, board);

            FormattedText hint = Text("Select a component to open its workspace", 10,
                Color.FromRgb(111, 143, 166), FontWeights.Normal);
            dc.DrawText(hint, new Point(board.Right - hint.Width - 18, board.Bottom - 25));
        }

        private void BuildLayouts(Rect board)
        {
            layouts.Clear();
            double smaLaneWidth = GetSmaLaneWidth(board);
            double left = board.Left + (board.Width < 620 ? 26 : 36);
            double nodeAreaRight = board.Right - smaLaneWidth;
            double nodeAreaWidth = nodeAreaRight - left;
            const double minimumGap = 12;
            double nodeWidth = Math.Max(88,
                Math.Min(138, (nodeAreaWidth - minimumGap * 3) / 4.0));
            double nodeHeight = 58;
            double top = board.Top + 28;
            double bottom = board.Bottom - nodeHeight - 42;
            double gap = Math.Max(minimumGap,
                (nodeAreaWidth - nodeWidth * 4) / 3.0);

            Add("gnss", "GNSS", "Gnss", left, top, nodeWidth, nodeHeight);
            Add("tod", "TIME-OF-DAY", "Gnss", left + nodeWidth + gap, top, nodeWidth, nodeHeight);
            Add("phc", "PHC", "Clock", left + (nodeWidth + gap) * 2, top, nodeWidth, nodeHeight);
            Add("system", "WINDOWS UTC", "Clock", left + (nodeWidth + gap) * 3, top, nodeWidth, nodeHeight);
            Add("atomic", "SA53 ATOMIC", "Atomic", left + nodeWidth * .45, bottom, nodeWidth, nodeHeight);
            Add("controller", "PCIe / FPGA", "Subsystems", left + (nodeWidth + gap) * 1.45, bottom, nodeWidth, nodeHeight);
            Add("i2c", "I2C FABRIC", "I2c", left + (nodeWidth + gap) * 2.55, bottom, nodeWidth, nodeHeight);
        }

        private static double GetSmaLaneWidth(Rect board)
        {
            return Math.Max(122, Math.Min(150, board.Width * .21));
        }

        private void Add(string key, string title, string workspace,
                         double x, double y, double width, double height)
        {
            layouts.Add(new NodeLayout
            {
                Key = key, Title = title, Workspace = workspace,
                Bounds = new Rect(x, y, width, height)
            });
        }

        private void DrawBoardTraces(DrawingContext dc, Rect board)
        {
            double smaLaneLeft = board.Right - GetSmaLaneWidth(board);
            Point[] main =
            {
                new Point(board.Left + 110, board.Top + 58),
                new Point(smaLaneLeft - 18, board.Top + 58)
            };
            Pen glow = new Pen(new SolidColorBrush(Color.FromArgb(45, 76, 201, 240)), 8);
            dc.DrawLine(glow, main[0], main[1]);
            Pen signal = new Pen(new SolidColorBrush(Color.FromRgb(76, 201, 240)), 2)
            {
                DashStyle = new DashStyle(new[] { 5.0, 6.0 }, dashOffset)
            };
            dc.DrawLine(signal, main[0], main[1]);

            Pen trace = new Pen(new SolidColorBrush(Color.FromArgb(90, 70, 117, 146)), 1.2);
            for (int index = 0; index < 4; index++)
            {
                double x = board.Left + board.Width * (.19 + index * .19);
                dc.DrawLine(trace, new Point(x, board.Top + 86),
                    new Point(x, board.Bottom - 48));
            }
            dc.DrawLine(trace, new Point(board.Left + 70, board.Bottom - 72),
                new Point(smaLaneLeft - 18, board.Bottom - 72));

            Pen laneDivider = new Pen(
                new SolidColorBrush(Color.FromArgb(105, 55, 104, 135)), 1)
            {
                DashStyle = new DashStyle(new[] { 2.0, 5.0 }, 0)
            };
            dc.DrawLine(laneDivider,
                new Point(smaLaneLeft, board.Top + 18),
                new Point(smaLaneLeft, board.Bottom - 18));
        }

        private void DrawNode(DrawingContext dc, NodeLayout layout)
        {
            HealthNode node = report == null ? null : report.Find(layout.Key);
            HealthSeverity severity = node == null ? HealthSeverity.Informational : node.Severity;
            Color color = SeverityColor(severity);
            bool hot = string.Equals(hotKey, layout.Key, StringComparison.OrdinalIgnoreCase);
            Brush fill = new SolidColorBrush(hot ? Color.FromRgb(26, 57, 79) : Color.FromRgb(13, 31, 49));
            dc.DrawRoundedRectangle(fill,
                new Pen(new SolidColorBrush(Color.FromArgb((byte)(hot ? 230 : 150), color.R, color.G, color.B)),
                    hot ? 1.8 : 1.0), layout.Bounds, 11, 11);
            dc.DrawEllipse(new SolidColorBrush(color), null,
                new Point(layout.Bounds.Left + 15, layout.Bounds.Top + 17), 4.5, 4.5);
            string title = layout.Key == "atomic" && node != null &&
                node.Name.StartsWith("mRO-50",
                    StringComparison.OrdinalIgnoreCase) ?
                "MRO-50 ATOMIC" : layout.Title;
            dc.DrawText(Text(title, 11, Colors.White, FontWeights.SemiBold),
                new Point(layout.Bounds.Left + 27, layout.Bounds.Top + 9));
            string status = node == null ? "WAITING" : node.Status;
            dc.DrawText(Text(status, 9, color, FontWeights.SemiBold),
                new Point(layout.Bounds.Left + 14, layout.Bounds.Top + 34));
        }

        private void DrawSmaConnectors(DrawingContext dc, Rect board)
        {
            HealthNode sma = report == null ? null : report.Find("sma");
            Color color = SeverityColor(sma == null ? HealthSeverity.Informational : sma.Severity);
            double laneLeft = board.Right - GetSmaLaneWidth(board);
            double x = board.Right - 26;
            FormattedText heading = Text("SMA I/O", 9, color,
                FontWeights.SemiBold);
            dc.DrawText(heading, new Point(
                laneLeft + (board.Right - laneLeft - heading.Width) / 2.0,
                board.Top + 13));
            Pen routePen = new Pen(
                new SolidColorBrush(Color.FromArgb(100, color.R, color.G, color.B)),
                1.0);
            for (int index = 0; index < 4; index++)
            {
                double y = board.Top + 60 + index * 43;
                dc.DrawLine(routePen, new Point(laneLeft + 12, y),
                    new Point(x - 13, y));
                dc.DrawEllipse(new SolidColorBrush(Color.FromRgb(38, 44, 48)),
                    new Pen(new SolidColorBrush(color), 1.4), new Point(x, y), 11, 11);
                dc.DrawEllipse(new SolidColorBrush(Color.FromRgb(6, 13, 20)), null,
                    new Point(x, y), 4, 4);
                SmaConnectorState state = index < smaStates.Length ? smaStates[index] : null;
                string route = state == null ? (index + 1).ToString(CultureInfo.InvariantCulture) :
                    (index + 1).ToString(CultureInfo.InvariantCulture) + " " +
                    (state.Direction == SmaDirection.Input ? "IN" :
                    state.Direction == SmaDirection.Output ? "OUT" : "OFF") +
                    " 0x" + state.Function.ToString("X4", CultureInfo.InvariantCulture);
                FormattedText routeText = Text(route, 8, color, FontWeights.Normal);
                dc.DrawText(routeText, new Point(x - routeText.Width - 16, y - 6));
            }
        }

        private void OnMouseMove(object sender, MouseEventArgs e)
        {
            Point point = e.GetPosition(this);
            NodeLayout layout = layouts.FirstOrDefault(item => item.Bounds.Contains(point));
            if (layout == null && point.X >
                ActualWidth - Math.Max(138, ActualWidth * .20))
            {
                hotKey = "sma";
                HealthNode sma = report == null ? null : report.Find("sma");
                string routes = string.Join("\n", smaStates.Where(item => item != null)
                    .Select(item => string.Format(CultureInfo.InvariantCulture,
                        "SMA {0}: {1}, function 0x{2:X4}", item.Connector,
                        item.Direction, item.Function)));
                ToolTip = sma == null ? "SMA timing I/O" : sma.Name + "\n" +
                    sma.Detail + (routes.Length == 0 ? string.Empty : "\n" + routes);
            }
            else
            {
                hotKey = layout == null ? null : layout.Key;
                HealthNode node = layout == null || report == null ? null : report.Find(layout.Key);
                ToolTip = node == null ? (layout == null ? null : layout.Title) :
                    node.Name + "\n" + node.Detail;
            }
            InvalidateVisual();
        }

        private void OnMouseLeftButtonUp(object sender, MouseButtonEventArgs e)
        {
            string workspace = null;
            NodeLayout layout = layouts.FirstOrDefault(item => item.Bounds.Contains(e.GetPosition(this)));
            if (layout != null)
                workspace = layout.Workspace;
            else if (string.Equals(hotKey, "sma", StringComparison.OrdinalIgnoreCase))
                workspace = "Sma";
            Request(workspace);
        }

        private void OnKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Enter || e.Key == Key.Space)
            {
                Request("Clock");
                e.Handled = true;
            }
        }

        private void Request(string workspace)
        {
            if (string.IsNullOrWhiteSpace(workspace))
                return;
            EventHandler<WorkspaceRequestEventArgs> handler = WorkspaceRequested;
            if (handler != null)
                handler(this, new WorkspaceRequestEventArgs(workspace));
        }

        private static FormattedText Text(string text, double size, Color color, FontWeight weight)
        {
            return new FormattedText(text ?? string.Empty, CultureInfo.InvariantCulture,
                FlowDirection.LeftToRight, new Typeface(new FontFamily("Segoe UI"),
                    FontStyles.Normal, weight, FontStretches.Normal), size,
                new SolidColorBrush(color), 1.0);
        }

        private static Color SeverityColor(HealthSeverity severity)
        {
            switch (severity)
            {
                case HealthSeverity.Healthy: return Color.FromRgb(105, 196, 65);
                case HealthSeverity.Attention: return Color.FromRgb(218, 181, 57);
                case HealthSeverity.Unavailable: return Color.FromRgb(255, 102, 122);
                default: return Color.FromRgb(76, 201, 240);
            }
        }
    }
}
