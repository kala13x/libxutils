/*!
 *  @file libxutils/examples/ntp.c
 *
 *  Copyleft (C) 2015  Sun Dro (a.k.a. kala13x)
 *
 * @brief Implementation of NTP functionality.
 */

#include <xutils/xstd.h>
#include <xutils/xtime.h>
#include <xutils/xlog.h>
#include <xutils/ntp.h>

#define NTP_SERVER "europe.pool.ntp.org"

int main() 
{
    xlog_defaults();
    xtime_t time;

    if (XNTP_GetDate(NTP_SERVER, 0, &time) > 0)
    {
        char sTime[32]; 
        XTime_ToHstr(&time, sTime, sizeof(sTime));
        xlog("Received NTP Date: %s", sTime);
        return 0;
    }

    xloge("Can not get time from NTP server: %s", NTP_SERVER);    
    return 1;
}
