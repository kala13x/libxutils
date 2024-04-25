/*!
 *  @file libxutils/examples/xcrpt.c
 * 
 *  This source is part of "libxutils" project
 *  2015-2024  Sun Dro (s.kalatoz@gmail.com)
 *
 * @brief Version and build number of the project.
 */

#include "xstd.h"
#include "xver.h"
#include <sys/xlog.h>

int main() 
{
    xlog_defaults();
    xlog("libxutils - v%s", XUtils_Version());
    return XSTDNON;
}