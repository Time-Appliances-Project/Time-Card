using System;
using System.Collections.Generic;

namespace TimeCardControlCenter
{
    /* Models.cs uses this transport-neutral compensation helper. */
    public static class CelesticaSensorMath
    {
        public static bool TryCompensateIcp10100(uint rawPressure,
            uint rawTemperature, IList<int> otp, out double pressurePascals)
        {
            pressurePascals = 0;
            if (otp == null || otp.Count < 4)
                return false;
            double t = rawTemperature - 32768.0;
            double quadratic = t * t / 16777216.0;
            double s1 = 3.5 * 1048576.0 + otp[0] * quadratic;
            double s2 = 2048.0 * otp[3] + otp[1] * quadratic;
            double s3 = 11.5 * 1048576.0 + otp[2] * quadratic;
            const double p1 = 45000.0;
            const double p2 = 80000.0;
            const double p3 = 105000.0;
            double denominator = s3 * (p1 - p2) + s1 * (p2 - p3) +
                s2 * (p3 - p1);
            if (Math.Abs(denominator) < 0.000001 ||
                Math.Abs(s1 - s2) < 0.000001)
                return false;
            double c = (s1 * s2 * (p1 - p2) +
                s2 * s3 * (p2 - p3) + s3 * s1 * (p3 - p1)) /
                denominator;
            double a = (p1 * s1 - p2 * s2 - (p2 - p1) * c) /
                (s1 - s2);
            double b = (p1 - a) * (s1 + c);
            double pressure = a + b / (c + rawPressure);
            if (double.IsNaN(pressure) || double.IsInfinity(pressure) ||
                pressure < 10000.0 || pressure > 130000.0)
                return false;
            pressurePascals = pressure;
            return true;
        }
    }
}
