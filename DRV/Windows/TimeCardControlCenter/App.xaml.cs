using System;
using System.IO;
using System.Threading.Tasks;
using System.Windows;

namespace TimeCardControlCenter
{
    public partial class App : Application
    {
        protected override async void OnStartup(StartupEventArgs e)
        {
            base.OnStartup(e);
            ShutdownMode = ShutdownMode.OnExplicitShutdown;

            SplashWindow splash = new SplashWindow();
            splash.Show();

            try
            {
                await Task.Delay(650);
                splash.SetStatus("Preparing hardware services");
                await Task.Delay(750);

                splash.SetStatus("Loading workspace modules");
                MainWindow controlCenter = new MainWindow();
                await Task.Delay(950);

                splash.SetStatus("Control Center ready");
                await Task.Delay(550);

                MainWindow = controlCenter;
                ShutdownMode = ShutdownMode.OnMainWindowClose;
                await splash.CloseAnimatedAsync();
                controlCenter.Show();
            }
            catch (Exception ex)
            {
                splash.Close();
                try
                {
                    File.WriteAllText(Path.Combine(Path.GetTempPath(),
                        "timecard-control-center-startup.log"), ex.ToString());
                }
                catch
                {
                }
                MessageBox.Show("Time Card Control Center could not start.\n\n" + ex.Message,
                    "Startup failed", MessageBoxButton.OK, MessageBoxImage.Error);
                Shutdown(-1);
            }
        }
    }
}
