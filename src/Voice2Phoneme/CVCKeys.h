// SPDX-License-Identifier: Apache-2.0

// Keys mapping to a common pattern of offsets for Consonant-Vowel-Consonant voicebank format of UTAU, a standard which results in a more natural sound

// TODO apparently generic UTAU banks follow a loose mapping standard, dynamically wired up by the oto.ini
// ...May need to rethink this approach for realtime! Or make it non-generic.

enum CVCKeys {
  A_BI_BU_BE_BO,
  A_BU_BO_PU_PA,
  A_CHI_TSU_TE_TO,
  A_DE_BA_BE_PA,
  A_DO_BA_BO_PA,
  A_E_KA_KE_SA,
  A_GA_ZA_DA_BA,
  A_GE_ZA_ZE_DA,
  A_GI_GU_GE_GO,
  A_GO_ZA_ZO_DA,
  A_GU_GO_ZU_ZA,
  A_HA_MA_YA_RA,
  A_HI_HU_HE_HO,
  A_HU_HO_MU_MA,
  A_I_U_E_O,
  A_ZI_ZU_ZE_ZO,
  A_KA_SA_TA_NA,
  A_KI_KU_KE_KO,
  A_KU_KO_TSU_SA,
  A_ME_YA_RA_WA,
  A_MI_MU_ME_MO,
  A_MO_YA_YO_RA,
  A_MU_MO_YU_YA,
  A_NE_HA_HE_MA,
  A_NI_NU_NE_NO,
  A_NO_HA_HO_MA,
  A_NU_NO_HU_HA,
  A_O_KA_KO_SA,
  A_PE_PE_GI_BO,
  A_PO_PI_PI_PE,
  A_RI_RU_RE_RO,
  A_RO_WA_WO,
  A_RU_RO_A_N,
  A_SE_TA_TE_NA,
  A_SHI_SU_SE_SO,
  A_SO_TA_TO_NA,
  A_SU_SO_TSU_TA,
  A_TSU_TO_NU_NA,
  A_U_O_KU_KA,
  A_WA_O_WO_A,
  A_YU_YO_RU_RA,
  A_ZU_ZO_BU_BA,
  E_GE_ZE_DE_BE,
  E_HE_ME_RE_N,
  E_I_YO_I_WO,
  E_KE_SE_TE_NE,
  E_RA_E_WO_E,
  E_YO_U_WO_N,
  E_YU_E_A_O,
  I_DA_PU_DA,
  I_DE_GA_PI_DO,
  I_E_KI_KE_SHI,
  I_GE_ZI_GO_ZI,
  I_GI_ZI_DI_BI,
  I_HI_MI_RI_N,
  I_KI_SHI_CHI_NI,
  I_ME_RI_RE,
  I_MO_RI_RO,
  I_NE_HI_HE_MI,
  I_NO_HI_HO_MI,
  I_O_KI_KO_SHI,
  I_PU_PU_PO_PO,
  I_SE_CHI_TE_NI,
  I_SO_CHI_TO_NI,
  I_YA_I_WA_I,
  I_YU_N_E_N,
  I_ZE_DO_DE_BI,
  I_ZO_BI_BE_PI,
  O_BE_BU_BI_BA,
  O_E_U_I_A,
  O_GE_GU_GI_GA,
  O_GO_ZO_DO_BO,
  O_GU_DE_PO_PA,
  O_HE_HU_HI_HA,
  O_HO_MO_YO_RO,
  O_KE_KU_KI_KA,
  O_KO_SO_TO_NO,
  O_ME_MU_MI_MA,
  O_NE_NU_NI_NA,
  O_PE_PU_PI_PA,
  O_RE_RU_RI_RA,
  O_SE_SU_SHI_SA,
  O_TE_TSU_CHI_TA,
  O_ZE_ZU_ZI_ZA,
  U_A_U_WA_U,
  U_DO_GI_PO_GA,
  U_GA_U_PE,
  U_GU_ZU_DU_BU,
  U_HU_MU_YU_RU,
  U_KU_SU_TSU_NU
}

//TODO need to map ends uniquely? They each provide a sustain.
//TODO mappings of the ~550 pairs into this set for file lookup? Likely need struct to populate with offsets.
//TODO mappings of the CVKeys to pairs to simplify matching?