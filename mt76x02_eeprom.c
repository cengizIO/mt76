/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 * Copyright (C) 2018 Lorenzo Bianconi <lorenzo.bianconi83@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <asm/unaligned.h>

#include "mt76.h"
#include "mt76x02_eeprom.h"
#include "mt76x02_regs.h"

static int
mt76x02_efuse_read(struct mt76_dev *dev, u16 addr, u8 *data,
		   enum mt76x02_eeprom_modes mode)
{
	u32 val;
	int i;

	val = __mt76_rr(dev, MT_EFUSE_CTRL);
	val &= ~(MT_EFUSE_CTRL_AIN |
		 MT_EFUSE_CTRL_MODE);
	val |= FIELD_PREP(MT_EFUSE_CTRL_AIN, addr & ~0xf);
	val |= FIELD_PREP(MT_EFUSE_CTRL_MODE, mode);
	val |= MT_EFUSE_CTRL_KICK;
	__mt76_wr(dev, MT_EFUSE_CTRL, val);

	if (!__mt76_poll_msec(dev, MT_EFUSE_CTRL, MT_EFUSE_CTRL_KICK,
			      0, 1000))
		return -ETIMEDOUT;

	udelay(2);

	val = __mt76_rr(dev, MT_EFUSE_CTRL);
	if ((val & MT_EFUSE_CTRL_AOUT) == MT_EFUSE_CTRL_AOUT) {
		memset(data, 0xff, 16);
		return 0;
	}

	for (i = 0; i < 4; i++) {
		val = __mt76_rr(dev, MT_EFUSE_DATA(i));
		put_unaligned_le32(val, data + 4 * i);
	}

	return 0;
}

int mt76x02_get_efuse_data(struct mt76_dev *dev, u16 base, void *buf,
			   int len, enum mt76x02_eeprom_modes mode)
{
	int ret, i;

	for (i = 0; i + 16 <= len; i += 16) {
		ret = mt76x02_efuse_read(dev, base + i, buf + i, mode);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt76x02_get_efuse_data);

void mt76x02_eeprom_parse_hw_cap(struct mt76_dev *dev)
{
	u16 val = mt76x02_eeprom_get(dev, MT_EE_NIC_CONF_0);

	switch (FIELD_GET(MT_EE_NIC_CONF_0_BOARD_TYPE, val)) {
	case BOARD_TYPE_5GHZ:
		dev->cap.has_5ghz = true;
		break;
	case BOARD_TYPE_2GHZ:
		dev->cap.has_2ghz = true;
		break;
	default:
		dev->cap.has_2ghz = true;
		dev->cap.has_5ghz = true;
		break;
	}
}
EXPORT_SYMBOL_GPL(mt76x02_eeprom_parse_hw_cap);

bool mt76x02_ext_pa_enabled(struct mt76_dev *dev, enum nl80211_band band)
{
	u16 conf0 = mt76x02_eeprom_get(dev, MT_EE_NIC_CONF_0);

	if (band == NL80211_BAND_5GHZ)
		return !(conf0 & MT_EE_NIC_CONF_0_PA_INT_5G);
	else
		return !(conf0 & MT_EE_NIC_CONF_0_PA_INT_2G);
}
EXPORT_SYMBOL_GPL(mt76x02_ext_pa_enabled);

void mt76x02_get_rx_gain(struct mt76_dev *dev, enum nl80211_band band,
			 u16 *rssi_offset, s8 *lna_2g, s8 *lna_5g)
{
	u16 val;

	val = mt76x02_eeprom_get(dev, MT_EE_LNA_GAIN);
	*lna_2g = val & 0xff;
	lna_5g[0] = val >> 8;

	val = mt76x02_eeprom_get(dev, MT_EE_RSSI_OFFSET_2G_1);
	lna_5g[1] = val >> 8;

	val = mt76x02_eeprom_get(dev, MT_EE_RSSI_OFFSET_5G_1);
	lna_5g[2] = val >> 8;

	if (!mt76x02_field_valid(lna_5g[1]))
		lna_5g[1] = lna_5g[0];

	if (!mt76x02_field_valid(lna_5g[2]))
		lna_5g[2] = lna_5g[0];

	if (band == NL80211_BAND_2GHZ)
		*rssi_offset = mt76x02_eeprom_get(dev, MT_EE_RSSI_OFFSET_2G_0);
	else
		*rssi_offset = mt76x02_eeprom_get(dev, MT_EE_RSSI_OFFSET_5G_0);
}
EXPORT_SYMBOL_GPL(mt76x02_get_rx_gain);

u8 mt76x02_get_lna_gain(struct mt76_dev *dev,
			s8 *lna_2g, s8 *lna_5g,
			struct ieee80211_channel *chan)
{
	u16 val;
	u8 lna;

	val = mt76x02_eeprom_get(dev, MT_EE_NIC_CONF_1);
	if (val & MT_EE_NIC_CONF_1_LNA_EXT_2G)
		*lna_2g = 0;
	if (val & MT_EE_NIC_CONF_1_LNA_EXT_5G)
		memset(lna_5g, 0, sizeof(s8) * 3);

	if (chan->band == NL80211_BAND_2GHZ)
		lna = *lna_2g;
	else if (chan->hw_value <= 64)
		lna = lna_5g[0];
	else if (chan->hw_value <= 128)
		lna = lna_5g[1];
	else
		lna = lna_5g[2];

	return lna != 0xff ? lna : 0;
}
EXPORT_SYMBOL_GPL(mt76x02_get_lna_gain);
