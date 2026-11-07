/* FTM5 System Info prefix, from fts_521/fts_lib/ftsCore.c readSysInfo(0).
 * Evidence only: do not infer firmware authenticity/calibration health from
 * version fields. No load-data, calibration, flash or reset commands here. */
#ifndef CEPHEUS_FTS_SYSINFO_H
#define CEPHEUS_FTS_SYSINFO_H
#include <stddef.h>
#include <stdint.h>
#define CE_FTS_SYSINFO_BYTES 208u
struct ceph_fts_sysinfo {
    uint16_t chip_id,fw_version,svn,cfg_version,cfg_project,cx_version,cx_project;
    uint16_t res_x,res_y;
    uint8_t cfg_afe,cx_afe,panel_afe,protocol,tx,rx;
    uint32_t fw_crc,cfg_crc;
};
static inline uint16_t ceph_fts_le16(const uint8_t *d)
{return (uint16_t)((uint16_t)d[0]|(uint16_t)d[1]<<8);}
static inline uint32_t ceph_fts_le32(const uint8_t *d)
{return (uint32_t)d[0]|(uint32_t)d[1]<<8|(uint32_t)d[2]<<16|(uint32_t)d[3]<<24;}
static inline int ceph_fts_sysinfo_decode(const uint8_t *d,size_t bytes,struct ceph_fts_sysinfo *out)
{
    if(!d || !out || bytes!=CE_FTS_SYSINFO_BYTES || d[0]!=0xa5 || d[1]!=1)return -1;
    struct ceph_fts_sysinfo s={0};
    s.chip_id=ceph_fts_le16(d+10);s.fw_version=ceph_fts_le16(d+16);
    s.svn=ceph_fts_le16(d+18);s.cfg_version=ceph_fts_le16(d+20);
    s.cfg_project=ceph_fts_le16(d+22);s.cx_version=ceph_fts_le16(d+24);
    s.cx_project=ceph_fts_le16(d+26);s.cfg_afe=d[28];s.cx_afe=d[29];
    s.panel_afe=d[30];s.protocol=d[31];s.fw_crc=ceph_fts_le32(d+64);
    s.cfg_crc=ceph_fts_le32(d+68);s.res_x=ceph_fts_le16(d+80);
    s.res_y=ceph_fts_le16(d+82);s.tx=d[84];s.rx=d[85];
    *out=s;return 0;
}
#endif
