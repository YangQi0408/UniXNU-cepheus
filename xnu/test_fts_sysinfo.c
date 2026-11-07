#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cepheus_fts_sysinfo.h"
int main(void)
{
    uint8_t d[CE_FTS_SYSINFO_BYTES];
    for(unsigned i=0;i<sizeof(d);++i)d[i]=(uint8_t)i;
    d[0]=0xa5;d[1]=1;
    struct ceph_fts_sysinfo s={0};
    assert(!ceph_fts_sysinfo_decode(d,sizeof(d),&s));
    assert(s.chip_id==0x0b0a && s.fw_version==0x1110 && s.svn==0x1312);
    assert(s.cfg_version==0x1514 && s.cfg_project==0x1716 && s.cx_version==0x1918 && s.cx_project==0x1b1a);
    assert(s.cfg_afe==28 && s.cx_afe==29 && s.panel_afe==30 && s.protocol==31);
    assert(s.fw_crc==0x43424140 && s.cfg_crc==0x47464544);
    assert(s.res_x==0x5150 && s.res_y==0x5352 && s.tx==84 && s.rx==85);
    struct ceph_fts_sysinfo saved=s;
    for(unsigned n=0;n<sizeof(d);++n)assert(ceph_fts_sysinfo_decode(d,n,&s)<0 && !memcmp(&s,&saved,sizeof(s)));
    assert(ceph_fts_sysinfo_decode(d,sizeof(d)+1,&s)<0 && !memcmp(&s,&saved,sizeof(s)));
    d[0]=0;assert(ceph_fts_sysinfo_decode(d,sizeof(d),&s)<0);d[0]=0xa5;
    d[1]=2;assert(ceph_fts_sysinfo_decode(d,sizeof(d),&s)<0 && !memcmp(&s,&saved,sizeof(s)));
    assert(ceph_fts_sysinfo_decode(NULL,sizeof(d),&s)<0);
    assert(ceph_fts_sysinfo_decode(d,sizeof(d),NULL)<0);
    puts("FTS System Info: exact 208-byte LE layout/header/identity, rejection atomicity PASS (synthetic data, no calibration verdict)");
}
