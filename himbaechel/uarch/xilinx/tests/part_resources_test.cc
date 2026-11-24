/*
 *  nextpnr -- Next Generation Place and Route
 *
 *  Copyright (C) 2026  nextpnr contributors
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <map>
#include <regex>
#include <sstream>
#include <string>
#include "command.h"
#include "gtest/gtest.h"
#include "nextpnr.h"
#include "uarch/xilinx/xilinx.h"
#define HIMBAECHEL_CONSTIDS "uarch/xilinx/constids.inc"
#include "himbaechel_constids.h"

USING_NEXTPNR_NAMESPACE

// The device utilisation counts the bels of a bucket that are not hidden.
// An xc7a35t is the xc7a50t die: the same chipdb, fewer LUTs, flip-flops,
// block RAMs and DSPs.  The expected numbers are what Vivado 2026.1 reports
// as available for each part (report_utilization of an empty design).
class XilinxPartResourcesTest : public ::testing::Test
{
  protected:
    virtual void SetUp() { init_share_dirname(); }

    dict<IdString, int> counted(const std::string &device)
    {
        ArchArgs chipArgs;
        chipArgs.device = device;
        Context ctx(chipArgs);
        ctx.uarch->init(&ctx);
        ctx.late_init();
        dict<IdString, int> result;
        for (auto bel : ctx.getBels()) {
            if (!ctx.getBelHidden(bel))
                result[ctx.getBelBucketForBel(bel)]++;
        }
        return result;
    }
};

TEST_F(XilinxPartResourcesTest, xc7a50t_counts_its_luts_once)
{
    auto n = counted("xc7a50tcsg324-1");
    EXPECT_EQ(n[id_SLICE_LUTX], 32600);
    EXPECT_EQ(n[id_SLICE_FFX], 65200);
    EXPECT_EQ(n[id_CARRY4], 8150);
    EXPECT_EQ(n[id_DSP48E1], 120);
    EXPECT_EQ(n[id_RAMB36E1], 75);
    EXPECT_EQ(n[id_RAMB18E1], 150);
}

TEST_F(XilinxPartResourcesTest, xc7a15t_counts_what_it_offers)
{
    auto n = counted("xc7a15tcsg324-1");
    EXPECT_EQ(n[id_SLICE_LUTX], 10400);
    EXPECT_EQ(n[id_SLICE_FFX], 20800);
    EXPECT_EQ(n[id_DSP48E1], 45);
    EXPECT_EQ(n[id_RAMB36E1], 25);
    EXPECT_EQ(n[id_RAMB18E1], 50);
}

TEST_F(XilinxPartResourcesTest, xc7a35t_counts_what_it_offers)
{
    auto n = counted("xc7a35tcsg324-1");
    EXPECT_EQ(n[id_SLICE_LUTX], 20800);
    EXPECT_EQ(n[id_SLICE_FFX], 41600);
    EXPECT_EQ(n[id_DSP48E1], 90);
    EXPECT_EQ(n[id_RAMB36E1], 50);
    EXPECT_EQ(n[id_RAMB18E1], 100);
    // Vivado limits these four only: the slices (one CARRY4 each) are the die's
    EXPECT_EQ(n[id_CARRY4], 8150);
}

TEST_F(XilinxPartResourcesTest, csg324_counts_its_bonded_io)
{
    for (auto device : {"xc7a35tcsg324-1", "xc7a50tcsg324-1"}) {
        auto n = counted(device);
        EXPECT_EQ(n[id_IOB33_INBUF_EN], 210) << device;
        EXPECT_EQ(n[id_IOB33_OUTBUF], 210) << device;
        EXPECT_EQ(n[id_ILOGICE3_IFF], 210) << device;
        EXPECT_EQ(n[id_OLOGICE3_OUTFF], 210) << device;
        // User I/O only. The two XADC pads are bonded IPAD sites and are
        // not in this row.
        EXPECT_EQ(n[id_PAD], 210) << device;
        // every IDELAY counts, bonded or not, as in Vivado
        EXPECT_EQ(n[id_IDELAYE2], 250) << device;
    }
}

// The JSON report is what apio reads. Cells on a type whose bels are all
// hidden (the constant drivers) must not appear as used with nothing
// available. These two are created and not placed, so they count as their
// type. A placed cell counts on the bel it occupies. PAD is the user I/O,
// not the XADC pads.
TEST_F(XilinxPartResourcesTest, report_used_stays_within_available)
{
    ArchArgs chipArgs;
    chipArgs.device = "xc7a35tcsg324-1";
    Context ctx(chipArgs);
    ctx.uarch->init(&ctx);
    ctx.late_init();
    ctx.createCell(ctx.id("$PACKER_GND_DRV"), id_PSEUDO_GND);
    ctx.createCell(ctx.id("$PACKER_VCC_DRV"), id_PSEUDO_VCC);
    std::ostringstream out;
    ctx.writeJsonReport(out);
    const std::string text = out.str();
    const bool gnd_is_absent = text.find("\"PSEUDO_GND\"") == std::string::npos;
    const bool vcc_is_absent = text.find("\"PSEUDO_VCC\"") == std::string::npos;
    EXPECT_TRUE(gnd_is_absent);
    EXPECT_TRUE(vcc_is_absent);
    // The root object is a std::map, so "fmax" is written before "utilization".
    // Take the utilization object by its braces.
    const size_t util_at = text.find("\"utilization\"");
    ASSERT_NE(util_at, std::string::npos);
    const size_t brace = text.find('{', util_at);
    ASSERT_NE(brace, std::string::npos);
    int depth = 0;
    size_t util_end = std::string::npos;
    for (size_t i = brace; i < text.size(); ++i) {
        const bool opens = text[i] == '{';
        const bool closes = text[i] == '}';
        if (opens)
            depth++;
        if (closes)
            depth--;
        const bool closed_the_object = closes && depth == 0;
        if (closed_the_object) {
            util_end = i;
            break;
        }
    }
    ASSERT_NE(util_end, std::string::npos);
    const std::string util = text.substr(brace, util_end - brace + 1);
    const std::regex row("\"([A-Za-z0-9_]+)\": \\{\"available\": (-?[0-9]+), \"used\": (-?[0-9]+)\\}");
    auto begin = std::sregex_iterator(util.begin(), util.end(), row);
    auto end = std::sregex_iterator();
    int rows = 0;
    bool saw_lut = false;
    bool saw_ff = false;
    bool saw_pad = false;
    bool saw_iob = false;
    for (auto it = begin; it != end; ++it) {
        const std::string name = (*it)[1].str();
        const int available = std::stoi((*it)[2].str());
        const int used = std::stoi((*it)[3].str());
        EXPECT_GE(used, 0) << name;
        EXPECT_LE(used, available) << name;
        if (name == "SLICE_LUTX") {
            EXPECT_EQ(available, 20800);
            saw_lut = true;
        }
        if (name == "SLICE_FFX") {
            EXPECT_EQ(available, 41600);
            saw_ff = true;
        }
        if (name == "PAD") {
            EXPECT_EQ(available, 210);
            saw_pad = true;
        }
        if (name == "IOB33_INBUF_EN") {
            EXPECT_EQ(available, 210);
            saw_iob = true;
        }
        rows++;
    }
    EXPECT_GT(rows, 10);
    EXPECT_TRUE(saw_lut);
    EXPECT_TRUE(saw_ff);
    EXPECT_TRUE(saw_pad);
    EXPECT_TRUE(saw_iob);
}

// A placed cell counts on the bel it occupies. The packer turns an IDDR into an
// ILOGICE3_IFF cell and an ODDR into an OLOGICE3_OUTFF cell and binds each to
// its site's flip-flop bel; in a High Performance bank those bels are
// ILOGICE2_IFF and OLOGICE2_OUTFF. Counted by cell type, the report had them
// on the E3 rows and nothing used on the E2 rows.
TEST_F(XilinxPartResourcesTest, report_counts_a_placed_cell_on_its_bel)
{
    ArchArgs chipArgs;
    chipArgs.device = "xc7k325tffg676-1";
    Context ctx(chipArgs);
    ctx.uarch->init(&ctx);
    ctx.late_init();
    BelId iff_bel;
    BelId outff_bel;
    for (auto bel : ctx.getBels()) {
        const std::string bucket = ctx.getBelBucketName(ctx.getBelBucketForBel(bel)).str(&ctx);
        const bool first_hp_iff = bucket == "ILOGICE2_IFF" && iff_bel == BelId();
        if (first_hp_iff)
            iff_bel = bel;
        const bool first_hp_outff = bucket == "OLOGICE2_OUTFF" && outff_bel == BelId();
        if (first_hp_outff)
            outff_bel = bel;
    }
    ASSERT_NE(iff_bel, BelId());
    ASSERT_NE(outff_bel, BelId());
    ctx.bindBel(iff_bel, ctx.createCell(ctx.id("iddr"), id_ILOGICE3_IFF), STRENGTH_LOCKED);
    ctx.bindBel(outff_bel, ctx.createCell(ctx.id("oddr"), id_OLOGICE3_OUTFF), STRENGTH_LOCKED);
    std::ostringstream out;
    ctx.writeJsonReport(out);
    const std::string text = out.str();
    std::map<std::string, std::pair<int, int>> rows;
    const std::regex row("\"([A-Za-z0-9_]+)\": \\{\"available\": (-?[0-9]+), \"used\": (-?[0-9]+)\\}");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), row); it != std::sregex_iterator(); ++it)
        rows[(*it)[1].str()] = std::make_pair(std::stoi((*it)[2].str()), std::stoi((*it)[3].str()));
    for (const auto &entry : rows)
        EXPECT_LE(entry.second.second, entry.second.first) << entry.first;
    ASSERT_EQ(rows.count("ILOGICE2_IFF"), 1u);
    ASSERT_EQ(rows.count("OLOGICE2_OUTFF"), 1u);
    EXPECT_EQ(rows.at("ILOGICE2_IFF").second, 1);
    EXPECT_EQ(rows.at("OLOGICE2_OUTFF").second, 1);
    const bool hr_iff_listed = rows.count("ILOGICE3_IFF") != 0;
    if (hr_iff_listed)
        EXPECT_EQ(rows.at("ILOGICE3_IFF").second, 0);
    const bool hr_outff_listed = rows.count("OLOGICE3_OUTFF") != 0;
    if (hr_outff_listed)
        EXPECT_EQ(rows.at("OLOGICE3_OUTFF").second, 0);
}

namespace {
std::map<std::string, std::pair<int, int>> utilization_rows(Context &ctx)
{
    std::ostringstream out;
    ctx.writeJsonReport(out);
    const std::string text = out.str();
    std::map<std::string, std::pair<int, int>> rows;
    const std::regex row("\"([A-Za-z0-9_]+)\": \\{\"available\": (-?[0-9]+), \"used\": (-?[0-9]+)\\}");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), row); it != std::sregex_iterator(); ++it)
        rows[(*it)[1].str()] = std::make_pair(std::stoi((*it)[2].str()), std::stoi((*it)[3].str()));
    return rows;
}

std::string site_prefix_of(Context &ctx, BelId bel)
{
    XilinxImpl *xi = static_cast<XilinxImpl *>(ctx.uarch.get());
    SiteIndex site = xi->get_bel_site(bel);
    const auto *extra = xi->tile_extra_data(site.tile);
    const bool site_ok = extra != nullptr && site.site >= 0 && site.site < extra->sites.ssize();
    if (!site_ok)
        return std::string();
    return IdString(extra->sites[site.site].name_prefix).str(&ctx);
}
} // namespace

// PAD available is the user I/O: the IOB input buffers, HR and HP together.
// On Artix that drops the GTP and XADC pads. On Spartan-7 and Zynq it was
// already that count. Kintex GTX pads are IPAD and OPAD sites too.
TEST_F(XilinxPartResourcesTest, pad_available_is_the_user_io)
{
    const char *devices[] = {"xc7a35tcsg324-1", "xc7a35tcpg236-1", "xc7a100tfgg676-1",
                             "xc7s50csga324-1", "xc7z010clg400-1", "xc7k325tffg676-1"};
    for (const char *device : devices) {
        auto n = counted(device);
        const int user_io = n[id_IOB33_INBUF_EN] + n[id_IOB18_INBUF_DCIEN];
        EXPECT_EQ(n[id_PAD], user_io) << device;
        EXPECT_GT(user_io, 0) << device;
    }
    auto cpg236 = counted("xc7a35tcpg236-1");
    EXPECT_EQ(cpg236[id_PAD], 106);
    EXPECT_EQ(cpg236[id_IOB33_INBUF_EN], 106);
}

// The PAD row of the used count is the cells on PAD bels, hidden or not: the
// report counts a placed cell on the bucket of the bel it occupies. A cell
// on the PAD bel of an IPAD or OPAD site (a GTP pad or the XADC VP/VN pair on
// Artix-7) therefore counts as PAD used, while PAD available leaves that bel
// out. The bel itself stays placeable.
TEST_F(XilinxPartResourcesTest, a_used_gtp_or_xadc_pad_counts_in_pad_used)
{
    for (const char *device : {"xc7a35tcsg324-1", "xc7a35tcpg236-1"}) {
        ArchArgs chipArgs;
        chipArgs.device = device;
        Context ctx(chipArgs);
        ctx.uarch->init(&ctx);
        ctx.late_init();
        BelId special = BelId();
        BelId user = BelId();
        for (auto bel : ctx.getBels()) {
            const bool is_pad = ctx.getBelType(bel) == id_PAD;
            if (!is_pad)
                continue;
            const std::string prefix = site_prefix_of(ctx, bel);
            const bool first_special = (prefix == "IPAD" || prefix == "OPAD") && special == BelId();
            if (first_special)
                special = bel;
            const bool first_user = prefix == "IOB" && !ctx.getBelHidden(bel) && user == BelId();
            if (first_user)
                user = bel;
        }
        ASSERT_NE(special, BelId()) << device;
        ASSERT_NE(user, BelId()) << device;
        EXPECT_TRUE(ctx.getBelHidden(special)) << device;
        EXPECT_FALSE(ctx.getBelHidden(user)) << device;
        auto before = utilization_rows(ctx);
        ASSERT_EQ(before.count("PAD"), 1u) << device;
        EXPECT_EQ(before.at("PAD").second, 0) << device;
        ctx.bindBel(special, ctx.createCell(ctx.id("special_pad"), id_PAD), STRENGTH_LOCKED);
        auto rows = utilization_rows(ctx);
        // The available count does not move, the used count does.
        EXPECT_EQ(rows.at("PAD").first, before.at("PAD").first) << device;
        EXPECT_EQ(rows.at("PAD").second, 1) << device;
        ctx.bindBel(user, ctx.createCell(ctx.id("user_pad"), id_PAD), STRENGTH_LOCKED);
        rows = utilization_rows(ctx);
        EXPECT_EQ(rows.at("PAD").second, 2) << device;
    }
}
