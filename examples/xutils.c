/*!
 *  @file libxutils/examples/xcrpt.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2024  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Version and build number of the project.
 */

#include <xutils/xstd.h>
#include <xutils/xver.h>
#include <xutils/xlog.h>

int main() 
{
    xlog_defaults();
    xlog("libxutils - v%s", XUtils_Version());
    return XSTDNON;
}