/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/reset-controller.h>
#include <dt-bindings/clock/qcom,gpucc-sdm660.h>

#include "clk-regmap-divider.h"
#include "clk-alpha-pll.h"
#include "common.h"
#include "clk-regmap.h"
#include "clk-pll.h"
#include "clk-rcg.h"
#include "clk-branch.h"

enum {
	P_CORE_BI_PLL_TEST_SE,
	P_GPLL0_OUT_MAIN,
	P_GPLL0_OUT_MAIN_DIV,
	P_GPU_PLL0_PLL_OUT_MAIN,
	P_GPU_PLL1_PLL_OUT_MAIN,
	P_XO,
};


static struct clk_branch gpucc_cxo_clk = {
	.halt_reg = 0x1020,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1020,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_cxo_clk",
			.parent_names = (const char *[]) {
				"cxo_a",
			},
			.num_parents = 1,
			.ops = &clk_branch2_ops,
		},
	},
};

static const struct parent_map gpucc_parent_map_0[] = {
	{ P_XO, 0 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_GPLL0_OUT_MAIN_DIV, 6 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gpucc_parent_names_0[] = {
	"cxo_a",
	"gcc_gpu_gpll0_clk",
	"gcc_gpu_gpll0_div_clk",
	"core_bi_pll_test_se",
};

static const struct parent_map gpucc_parent_map_1[] = {
	{ P_XO, 0 },
	{ P_GPU_PLL0_PLL_OUT_MAIN, 1 },
	{ P_GPU_PLL1_PLL_OUT_MAIN, 3 },
	{ P_GPLL0_OUT_MAIN, 5 },
	{ P_CORE_BI_PLL_TEST_SE, 7 },
};

static const char * const gpucc_parent_names_1[] = {
	"xo",
	"gpu_pll0_pll_out_main",
	"gpu_pll1_pll_out_main",
	"gcc_gpu_gpll0_clk",
	"core_bi_pll_test_se",
};

static struct pll_vco gpu_vco[] = {
	{ 1000000000, 2000000000, 0 },
	{ 500000000,  1000000000, 2 },
	{ 250000000,   500000000, 3 },
};

/* 800MHz configuration */
static const struct alpha_pll_config gpu_pll0_config = {
	.l = 0x29,
	.config_ctl_val = 0x4001055b,
	.alpha = 0xaaaaab00,
	.alpha_hi = 0xaa,
	.alpha_en_mask = BIT(24),
	.vco_val = 0x2 << 20,
	.vco_mask = 0x3 << 20,
	.main_output_mask = 0x1,
};

// static struct pll_vco_data pll_data[] = {
// 	/* Frequency  post-div */
// 	{ 640000000,  0x1 },
// };

static struct clk_alpha_pll gpu_pll0_pll_out_main = {
	.offset = 0x0,
	.vco_table = gpu_vco,
	.num_vco = ARRAY_SIZE(gpu_vco),
	// .vco_data = pll_data,
	// .num_vco_data = ARRAY_SIZE(pll_data),
	.clkr.hw.init = &(struct clk_init_data){
			.name = "gpu_pll0_pll_out_main",
			.parent_hws = (const struct clk_hw *[]){ &gpucc_cxo_clk.clkr.hw },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},

};

static struct clk_alpha_pll gpu_pll1_pll_out_main = {
	.offset = 0x40,
	.vco_table = gpu_vco,
	.num_vco = ARRAY_SIZE(gpu_vco),
	// .vco_data = pll_data,
	// .num_vco_data = ARRAY_SIZE(pll_data),
	.clkr.hw.init = &(struct clk_init_data){
			.name = "gpu_pll1_pll_out_main",
			.parent_hws = (const struct clk_hw *[]){ &gpucc_cxo_clk.clkr.hw },
			.num_parents = 1,
			.ops = &clk_alpha_pll_ops,
		},

};

/* GFX clock init data */
static struct clk_init_data gpu_clks_init[] = {
	[0] = {
		.name = "gfx3d_clk_src",
		.parent_names = gpucc_parent_names_1,
		.num_parents = 3,
		.ops = &clk_gfx3d_src_ops,
		.flags = CLK_SET_RATE_PARENT,
	},
	[1] = {
		.name = "gpucc_gfx3d_clk",
		.parent_names = (const char *[]){
			"gfx3d_clk_src",
		},
		.num_parents = 1,
		.ops = &clk_branch2_ops,
		.flags = CLK_SET_RATE_PARENT,
		// .vdd_class = &vdd_gfx,
	},
};

/*
 * Frequencies and PLL configuration
 * The PLL source would be to ping-pong between GPU-PLL0
 * and GPU-PLL1.
 *  ====================================================
 *  | F         | PLL SRC Freq | PLL postdiv | RCG Div |
 *  ====================================================
 *  | 160000000 | 640000000    |    2        |    2    |
 *  | 266000000 | 532000000    |    1        |    2    |
 *  | 370000000 | 740000000    |    1        |    2    |
 *  | 465000000 | 930000000    |    1        |    2    |
 *  | 588000000 | 1176000000   |    1        |    2    |
 *  | 647000000 | 1294000000   |    1        |    2    |
 *  | 700000000 | 1400000000   |    1        |    2    |
 *  | 750000000 | 1500000000   |    1        |    2    |
 *  ====================================================
*/

static const struct freq_tbl ftbl_gfx3d_clk_src[] = {
	F( 19200000, 0,  1, 0, 0),
	F(160000000, 0,  2, 0, 0),
	F(266000000, 0,  2, 0, 0),
	F(370000000, 0,  2, 0, 0),
	F(430000000, 0,  2, 0, 0),
	F(465000000, 0,  2, 0, 0),
	F(585000000, 0,  2, 0, 0),
	F(588000000, 0,  2, 0, 0),
	F(647000000, 0,  2, 0, 0),
	F(700000000, 0,  2, 0, 0),
	F(750000000, 0,  2, 0, 0),
	{ }
};

static const struct freq_tbl ftbl_gfx3d_clk_src_630[] = {
	F( 19200000, 0,  1, 0, 0),
	F(160000000, 0,  2, 0, 0),
	F(240000000, 0,  2, 0, 0),
	F(370000000, 0,  2, 0, 0),
	F(465000000, 0,  2, 0, 0),
	F(588000000, 0,  2, 0, 0),
	F(647000000, 0,  2, 0, 0),
	F(700000000, 0,  2, 0, 0),
	F(775000000, 0,  2, 0, 0),
	{ }
};

static struct clk_rcg2 gfx3d_clk_src = {
	.cmd_rcgr = 0x1070,
	.mnd_width = 0,
	.hid_width = 5,
	.freq_tbl = ftbl_gfx3d_clk_src,
	.parent_map = gpucc_parent_map_1,
	.clkr.hw.init = &gpu_clks_init[0],
};

static const struct freq_tbl ftbl_rbbmtimer_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	{ }
};

static struct clk_rcg2 rbbmtimer_clk_src = {
	.cmd_rcgr = 0x10b0,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpucc_parent_map_0,
	.freq_tbl = ftbl_rbbmtimer_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "rbbmtimer_clk_src",
		.parent_names = gpucc_parent_names_0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};

static const struct freq_tbl ftbl_rbcpr_clk_src[] = {
	F(19200000, P_XO, 1, 0, 0),
	F(50000000, P_GPLL0_OUT_MAIN_DIV, 6, 0, 0),
	{ }
};

static struct clk_rcg2 rbcpr_clk_src = {
	.cmd_rcgr = 0x1030,
	.mnd_width = 0,
	.hid_width = 5,
	.parent_map = gpucc_parent_map_0,
	.freq_tbl = ftbl_rbcpr_clk_src,
	.clkr.hw.init = &(struct clk_init_data){
		.name = "rbcpr_clk_src",
		.parent_names = gpucc_parent_names_0,
		.num_parents = 4,
		.ops = &clk_rcg2_ops,
	},
};



static struct clk_branch gpucc_gfx3d_clk = {
	.halt_reg = 0x1098,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1098,
		.enable_mask = BIT(0),
		.hw.init = &gpu_clks_init[1],
	},
};

static struct clk_branch gpucc_rbbmtimer_clk = {
	.halt_reg = 0x10d0,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x10d0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_rbbmtimer_clk",
			.parent_names = (const char *[]){
				"rbbmtimer_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_branch gpucc_rbcpr_clk = {
	.halt_reg = 0x1054,
	.halt_check = BRANCH_HALT,
	.clkr = {
		.enable_reg = 0x1054,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "gpucc_rbcpr_clk",
			.parent_names = (const char *[]){
				"rbcpr_clk_src",
			},
			.num_parents = 1,
			.flags = CLK_SET_RATE_PARENT,
			.ops = &clk_branch2_ops,
		},
	},
};

static struct clk_regmap *gpucc_660_clocks[] = {
	[GPU_PLL0_PLL] = &gpu_pll0_pll_out_main.clkr,
	[GPU_PLL1_PLL] = &gpu_pll1_pll_out_main.clkr,
	[GFX3D_CLK_SRC] = &gfx3d_clk_src.clkr,
	[GPUCC_GFX3D_CLK] = &gpucc_gfx3d_clk.clkr,
	[GPUCC_RBBMTIMER_CLK] = &gpucc_rbbmtimer_clk.clkr,
	[RBBMTIMER_CLK_SRC] = &rbbmtimer_clk_src.clkr,
	[GPUCC_CXO_CLK] = &gpucc_cxo_clk.clkr,
	[RBCPR_CLK_SRC] = &rbcpr_clk_src.clkr,
	[GPUCC_RBCPR_CLK] = &gpucc_rbcpr_clk.clkr,
};

static const struct regmap_config gpucc_660_regmap_config = {
	.reg_bits	= 32,
	.reg_stride	= 4,
	.val_bits	= 32,
	.max_register	= 0x9034,
	.fast_io	= true,
};

static const struct qcom_cc_desc gpucc_660_desc = {
	.config = &gpucc_660_regmap_config,
	.clks = gpucc_660_clocks,
	.num_clks = ARRAY_SIZE(gpucc_660_clocks),
};

static const struct of_device_id gpucc_660_match_table[] = {
	{ .compatible = "qcom,gpucc-sdm660" },
	{ .compatible = "qcom,gpucc-sdm630" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpucc_660_match_table);



static int gpucc_660_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct regmap *regmap;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "Failed to get resources\n");
		return -EINVAL;
	}


	regmap = qcom_cc_map(pdev, &gpucc_660_desc);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);


	/* GFX rail fmax data linked to branch clock */

	clk_alpha_pll_configure(&gpu_pll0_pll_out_main, regmap,
							&gpu_pll0_config);
	clk_alpha_pll_configure(&gpu_pll1_pll_out_main, regmap,
							&gpu_pll0_config);

	ret = qcom_cc_really_probe(&pdev->dev, &gpucc_660_desc, regmap);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register GPUCC clocks\n");
		return ret;
	}

	clk_prepare_enable(gpucc_cxo_clk.clkr.hw.clk);

	dev_info(&pdev->dev, "Registered GPUCC clocks\n");

	return ret;
}

static struct platform_driver gpucc_660_driver = {
	.probe		= gpucc_660_probe,
	.driver		= {
		.name	= "gpucc-sdm660",
		.of_match_table = gpucc_660_match_table,
	},
};

static int __init gpucc_660_init(void)
{
	return platform_driver_register(&gpucc_660_driver);
}
arch_initcall(gpucc_660_init);

static void __exit gpucc_660_exit(void)
{
	platform_driver_unregister(&gpucc_660_driver);
}
module_exit(gpucc_660_exit);



