using System;
using System.Reflection;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media.Animation;

namespace TimeCardControlCenter
{
    public partial class SplashWindow : Window
    {
        public SplashWindow()
        {
            InitializeComponent();
            Version version = Assembly.GetExecutingAssembly().GetName().Version;
            VersionText.Text = string.Format("VERSION {0}.{1}.{2}",
                version.Major, version.Minor, version.Build);
        }

        public void SetStatus(string status)
        {
            SplashStatusText.Text = status;
        }

        public async Task CloseAnimatedAsync()
        {
            DoubleAnimation fade = new DoubleAnimation
            {
                To = 0,
                Duration = TimeSpan.FromMilliseconds(220),
                EasingFunction = new CubicEase { EasingMode = EasingMode.EaseIn }
            };
            SplashRoot.BeginAnimation(OpacityProperty, fade);
            await Task.Delay(230);
            Close();
        }
    }
}
