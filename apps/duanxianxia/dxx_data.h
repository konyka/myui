/**
 * @file dxx_data.h
 * @brief duanxianxia clone: static snapshot data (M14b).
 *
 * Index quotes: 5 of 12 rows are REAL values fetched from sina
 * (hq.sinajs.cn) on 2026-08-12 ~14:20 CST (上证指数/深证成指/创业板指/
 * 上证50/沪深300; the two sh* rows had their change computed from the
 * previous close). The HK/futures/CSI2000/FTSE-A50 rows came back empty
 * from the endpoint, so they hold plausible static approximations —
 * marked "约值" below. See docs/apps/duanxianxia.md.
 */
#ifndef DXX_DATA_H
#define DXX_DATA_H

/** @brief One index quote row (name + last + change%). */
typedef struct dxx_index_quote_t {
  const char* name;    /**< static string */
  double value;        /**< last price / index value */
  double change_pct;   /**< signed percent, e.g. +0.21 */
} dxx_index_quote_t;

#define DXX_INDEX_COUNT 12

/** @brief The 12 indices of the header strip, in site order. */
extern const dxx_index_quote_t DXX_INDICES[DXX_INDEX_COUNT];

/** @brief Footer disclaimer line (verbatim from the site). */
extern const char* const DXX_FOOTER_DISCLAIMER;
/** @brief Footer ICP/contact line (verbatim from the site). */
extern const char* const DXX_FOOTER_ICP;

#endif /* DXX_DATA_H */
