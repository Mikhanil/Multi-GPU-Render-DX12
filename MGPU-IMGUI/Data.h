#pragma once

constexpr float_t atlasWidth{ 512.0f };
constexpr float_t atlasHeight{ 4256.0f };

constexpr float_t step{ 32.0f };

constexpr uint16_t SKILL_PICTURES_COUNT{ 63 };
constexpr uint16_t NAVIGATION_ICONS_COUNT{ 2 };
constexpr uint16_t OVERALL_UI_ELEMENTS = SKILL_PICTURES_COUNT + NAVIGATION_ICONS_COUNT;

constexpr char* SKILL_PICURES_PLACEMENTS[SKILL_PICTURES_COUNT] = {
	"Pictures\\SpellIconsVolume_1_Free\\Fire\\Fire_7.png",
	"Pictures\\SpellIconsVolume_1_Free\\Fire\\Fire_8.png",
	"Pictures\\SpellIconsVolume_1_Free\\Fire\\Fire_9.png",
	"Pictures\\SpellIconsVolume_1_Free\\Fire\\Fire_10.png",
	"Pictures\\SpellIconsVolume_1_Free\\Fire\\Fire_11.png",
	"Pictures\\SpellIconsVolume_1_Free\\Fire\\Fire_12.png",
	"Pictures\\SpellIconsVolume_1_Free\\Fire\\Fire_13.png",
	"Pictures\\SpellIconsVolume_1_Free\\Fire\\Fire_14.png",
	"Pictures\\SpellIconsVolume_1_Free\\Fire\\Fire_28.png",
	"Pictures\\SpellIconsVolume_1_Free\\Dark\\Dark_6.png",
	"Pictures\\SpellIconsVolume_1_Free\\Dark\\Dark_7.png",
	"Pictures\\SpellIconsVolume_1_Free\\Dark\\Dark_8.png",
	"Pictures\\SpellIconsVolume_1_Free\\Dark\\Dark_9.png",
	"Pictures\\SpellIconsVolume_1_Free\\Dark\\Dark_10.png",
	"Pictures\\SpellIconsVolume_1_Free\\Dark\\Dark_11.png",
	"Pictures\\SpellIconsVolume_1_Free\\Dark\\Dark_12.png",
	"Pictures\\SpellIconsVolume_1_Free\\Dark\\Dark_13.png",
	"Pictures\\SpellIconsVolume_1_Free\\Dark\\Dark_14.png",
	"Pictures\\SpellIconsVolume_1_Free\\Holy\\Holy_5.png",
	"Pictures\\SpellIconsVolume_1_Free\\Holy\\Holy_6.png",
	"Pictures\\SpellIconsVolume_1_Free\\Holy\\Holy_7.png",
	"Pictures\\SpellIconsVolume_1_Free\\Holy\\Holy_8.png",
	"Pictures\\SpellIconsVolume_1_Free\\Holy\\Holy_9.png",
	"Pictures\\SpellIconsVolume_1_Free\\Holy\\Holy_10.png",
	"Pictures\\SpellIconsVolume_1_Free\\Holy\\Holy_11.png",
	"Pictures\\SpellIconsVolume_1_Free\\Holy\\Holy_12.png",
	"Pictures\\SpellIconsVolume_1_Free\\Holy\\Holy_13.png",
	"Pictures\\SpellIconsVolume_1_Free\\Holy\\Holy_14.png",
	"Pictures\\SpellIconsVolume_1_Free\\Nature\\Nature_2.png",
	"Pictures\\SpellIconsVolume_1_Free\\Nature\\Nature_4.png",
	"Pictures\\SpellIconsVolume_1_Free\\Nature\\Nature_6.png",
	"Pictures\\SpellIconsVolume_1_Free\\Nature\\Nature_7.png",
	"Pictures\\SpellIconsVolume_1_Free\\Nature\\Nature_8.png",
	"Pictures\\SpellIconsVolume_1_Free\\Nature\\Nature_9.png",
	"Pictures\\SpellIconsVolume_1_Free\\Nature\\Nature_10.png",
	"Pictures\\SpellIconsVolume_1_Free\\Nature\\Nature_11.png",
	"Pictures\\SpellIconsVolume_1_Free\\Nature\\Nature_12.png",
	"Pictures\\SpellIconsVolume_1_Free\\Nature\\Nature_13.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_1.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_2.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_3.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_4.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_5.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_6.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_7.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_8.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_9.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_10.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_11.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_12.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_13.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_14.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_15.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_16.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_17.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_18.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_19.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_20.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_21.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_22.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_23.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_24.png",
	"Pictures\\SpellIconsVolume_1_Free\\Blood\\BloodMage_25.png",
};

constexpr char* NAVIGATION_ICONS_PLACEMENTS[NAVIGATION_ICONS_COUNT] = {
	"Pictures\\Icons\\Chest.png",
	"Pictures\\Icons\\guild.png",
};

constexpr char* MAP_PLACEMENT = "Pictures\\map.png";