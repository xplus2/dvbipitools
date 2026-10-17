/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/bim/fragment/fragment_int.h"

static const bim_part_t t_TitleMediaType_k1[] = {
  {BIM_PART_ELEM, "TitleImage", &t_ImageLocatorType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "TitleVideo", &t_TemporalSegmentLocatorType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "TitleAudio", &t_TemporalSegmentLocatorType, 0, 1, NULL, 0},
};
static const bim_part_t t_TitleMediaType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_TitleMediaType_k1, 3};
const bim_type_t t_TitleMediaType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_TitleMediaType_top};

static const bim_attr_t t_ShortTitleType_attrs[] = {
  {"length", BIM_UVLU8, 1, NULL, 0},
  {"type", BIM_TERMREF, 0, NULL, 0},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_ShortTitleType = {t_ShortTitleType_attrs, 3, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_TextualType_attrs[] = {
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_TextualType = {t_TextualType_attrs, 1, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const char *const t_SynopsisType_e0[] = {"short", "medium", "long"};
static const bim_attr_t t_SynopsisType_attrs[] = {
  {"length", BIM_ENUM, 0, t_SynopsisType_e0, 3},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_SynopsisType = {t_SynopsisType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const char *const t_KeywordType_e0[] = {"main", "secondary", "other"};
static const bim_attr_t t_KeywordType_attrs[] = {
  {"type", BIM_ENUM, 0, t_KeywordType_e0, 3},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_KeywordType = {t_KeywordType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_TermNameType_attrs[] = {
  {"preferred", BIM_BOOLEAN, 0, NULL, 0},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_TermNameType = {t_TermNameType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_ControlledTermType_attrs[] = {
  {"href", BIM_TERMREF, 1, NULL, 0},
};
static const bim_part_t t_ControlledTermType_k1[] = {
  {BIM_PART_ELEM, "Name", &t_TermNameType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Definition", &t_TextualType, 0, 1, NULL, 0},
};
static const bim_part_t t_ControlledTermType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ControlledTermType_k1, 2};
const bim_type_t t_ControlledTermType = {t_ControlledTermType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ControlledTermType_top};

static const char *const t_GenreType_e1[] = {"main", "secondary", "other"};
static const bim_attr_t t_GenreType_attrs[] = {
  {"href", BIM_TERMREF, 1, NULL, 0},
  {"type", BIM_ENUM, 0, t_GenreType_e1, 3},
};
static const bim_part_t t_GenreType_k1[] = {
  {BIM_PART_ELEM, "Name", &t_TermNameType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Definition", &t_TextualType, 0, 1, NULL, 0},
};
static const bim_part_t t_GenreType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_GenreType_k1, 2};
const bim_type_t t_GenreType = {t_GenreType_attrs, 2, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_GenreType_top};

static const bim_attr_t t_InlineTermDefinitionType_Name_attrs[] = {
  {"preferred", BIM_BOOLEAN, 0, NULL, 0},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_InlineTermDefinitionType_Name = {t_InlineTermDefinitionType_Name_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_ControlledTermUseType_attrs[] = {
  {"href", BIM_TERMREF, 1, NULL, 0},
};
static const bim_part_t t_ControlledTermUseType_k1[] = {
  {BIM_PART_ELEM, "Name", &t_InlineTermDefinitionType_Name, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Definition", &t_TextualType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_ControlledTermUseType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ControlledTermUseType_k1, 2};
const bim_type_t t_ControlledTermUseType = {t_ControlledTermUseType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ControlledTermUseType_top};

static const bim_part_t t_ParentalGuidanceType_k1[] = {
  {BIM_PART_ELEM, "ParentalRating", &t_ControlledTermUseType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MinimumAge", &t_xs_UVLU8, 1, 1, NULL, 0},
};
static const bim_part_t t_ParentalGuidanceType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 1, 1, t_ParentalGuidanceType_k1, 2},
  {BIM_PART_ELEM, "Region", &t_xs_STRING, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_ParentalGuidanceType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ParentalGuidanceType_k2, 2};
const bim_type_t t_ParentalGuidanceType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ParentalGuidanceType_top};

static const char *const t_ExtendedLanguageType_e1[] = {"original", "dubbed", "background"};
static const bim_attr_t t_ExtendedLanguageType_attrs[] = {
  {"supplemental", BIM_BOOLEAN, 0, NULL, 0},
  {"type", BIM_ENUM, 0, t_ExtendedLanguageType_e1, 3},
};
const bim_type_t t_ExtendedLanguageType = {t_ExtendedLanguageType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_CaptionLanguageType_attrs[] = {
  {"closed", BIM_BOOLEAN, 0, NULL, 0},
  {"supplemental", BIM_BOOLEAN, 0, NULL, 0},
};
const bim_type_t t_CaptionLanguageType = {t_CaptionLanguageType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_SignLanguageType_attrs[] = {
  {"primary", BIM_BOOLEAN, 0, NULL, 0},
  {"translation", BIM_BOOLEAN, 0, NULL, 0},
  {"type", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_SignLanguageType = {t_SignLanguageType_attrs, 3, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_NameComponentType_attrs[] = {
  {"abbrev", BIM_STRING, 0, NULL, 0},
  {"initial", BIM_STRING, 0, NULL, 0},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_NameComponentType = {t_NameComponentType_attrs, 3, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const char *const t_PersonNameType_e2[] = {"former", "variant", "main"};
static const bim_attr_t t_PersonNameType_attrs[] = {
  {"dateFrom", BIM_STRING, 0, NULL, 0},
  {"dateTo", BIM_STRING, 0, NULL, 0},
  {"type", BIM_ENUM, 0, t_PersonNameType_e2, 3},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_part_t t_PersonNameType_k1[] = {
  {BIM_PART_ELEM, "GivenName", &t_NameComponentType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "FamilyName", &t_NameComponentType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Title", &t_NameComponentType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Numeration", &t_xs_STRING, 0, 1, NULL, 0},
};
static const bim_part_t t_PersonNameType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 1, BIM_UNBOUNDED, t_PersonNameType_k1, 4},
};
static const bim_part_t t_PersonNameType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_PersonNameType_k2, 1};
const bim_type_t t_PersonNameType = {t_PersonNameType_attrs, 4, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_PersonNameType_top};

static const bim_attr_t t_TVAIDRefElementType_attrs[] = {
  {"ref", BIM_STRING, 1, NULL, 0},
};
const bim_type_t t_TVAIDRefElementType = {t_TVAIDRefElementType_attrs, 1, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_attr_t t_CreditsItemType_attrs[] = {
  {"role", BIM_TERMREF, 1, NULL, 0},
};
static const bim_part_t t_CreditsItemType_k1[] = {
  {BIM_PART_ELEM, "PersonName", &t_PersonNameType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "PersonNameIDRef", &t_TVAIDRefElementType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "OrganizationName", &t_TextualType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "OrganizationNameIDRef", &t_TVAIDRefElementType, 1, 1, NULL, 0},
};
static const bim_part_t t_CreditsItemType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 0, BIM_UNBOUNDED, t_CreditsItemType_k1, 4},
};
static const bim_part_t t_CreditsItemType_k3[] = {
  {BIM_PART_ELEM, "Character", &t_PersonNameType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_CreditsItemType_k4[] = {
  {BIM_PART_SEQ, NULL, NULL, 1, 1, t_CreditsItemType_k2, 1},
  {BIM_PART_SEQ, NULL, NULL, 1, 1, t_CreditsItemType_k3, 1},
};
static const bim_part_t t_CreditsItemType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_CreditsItemType_k4, 2};
const bim_type_t t_CreditsItemType = {t_CreditsItemType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_CreditsItemType_top};

static const bim_part_t t_CreditsListType_k1[] = {
  {BIM_PART_ELEM, "CreditsItem", &t_CreditsItemType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_CreditsListType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_CreditsListType_k1, 1};
const bim_type_t t_CreditsListType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_CreditsListType_top};

static const bim_part_t t_AwardType_k1[] = {
  {BIM_PART_ELEM, "Nominee", &t_CreditsItemType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "Recipient", &t_CreditsItemType, 1, 1, NULL, 0},
};
static const bim_part_t t_AwardType_k2[] = {
  {BIM_PART_ELEM, "Category", &t_TextualType, 1, 1, NULL, 0},
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_AwardType_k1, 2},
};
static const bim_part_t t_AwardType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_AwardType_k2, 2};
const bim_type_t t_AwardType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_AwardType_top};

static const bim_part_t t_AwardsListItemType_k1[] = {
  {BIM_PART_ELEM, "Title", &t_TextualType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "Year", &t_xs_UNSUPPORTED, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "Award", &t_AwardType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_AwardsListItemType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_AwardsListItemType_k1, 3};
const bim_type_t t_AwardsListItemType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_AwardsListItemType_top};

static const bim_part_t t_AwardsListType_k1[] = {
  {BIM_PART_ELEM, "AwardsListItem", &t_AwardsListItemType, 1, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_AwardsListType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_AwardsListType_k1, 1};
const bim_type_t t_AwardsListType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_AwardsListType_top};

static const char *const t_SegmentReferenceType_e1[] = {"segment", "segmentgroup"};
static const bim_attr_t t_SegmentReferenceType_attrs[] = {
  {"ref", BIM_STRING, 1, NULL, 0},
  {"segmentType", BIM_ENUM, 0, t_SegmentReferenceType_e1, 2},
};
const bim_type_t t_SegmentReferenceType = {t_SegmentReferenceType_attrs, 2, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_RelatedMaterialType_k1[] = {
  {BIM_PART_ELEM, "MediaLocator", &t_MediaLocatorType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "SegmentReference", &t_SegmentReferenceType, 1, 1, NULL, 0},
};
static const bim_part_t t_RelatedMaterialType_k2[] = {
  {BIM_PART_ELEM, "HowRelated", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Format", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_CHOICE, NULL, NULL, 1, 1, t_RelatedMaterialType_k1, 2},
  {BIM_PART_ELEM, "PromotionalText", &t_TextualType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "PromotionalMedia", &t_TitleMediaType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "SourceMediaLocator", &t_MediaLocatorType, 0, 1, NULL, 0},
};
static const bim_part_t t_RelatedMaterialType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_RelatedMaterialType_k2, 6};
const bim_type_t t_RelatedMaterialType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_RelatedMaterialType_top};

static const bim_part_t t_TVATimeType_k1[] = {
  {BIM_PART_ELEM, "TimePoint", &t_xs_STRING, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "Duration", &t_xs_STRING, 0, 1, NULL, 0},
};
static const bim_part_t t_TVATimeType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_TVATimeType_k1, 2};
const bim_type_t t_TVATimeType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_TVATimeType_top};

static const bim_part_t t_CreationCoordinatesType_k1[] = {
  {BIM_PART_ELEM, "CreationDate", &t_TVATimeType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "CreationLocation", &t_xs_STRING, 0, 1, NULL, 0},
};
static const bim_part_t t_CreationCoordinatesType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_CreationCoordinatesType_k1, 2};
const bim_type_t t_CreationCoordinatesType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_CreationCoordinatesType_top};

static const bim_attr_t t_HeaderType_attrs[] = {
  {"id", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_HeaderType = {t_HeaderType_attrs, 1, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_attr_t t_TermUseType_attrs[] = {
  {"href", BIM_TERMREF, 0, NULL, 0},
};
static const bim_part_t t_TermUseType_k1[] = {
  {BIM_PART_ELEM, "Name", &t_InlineTermDefinitionType_Name, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Definition", &t_TextualType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_TermUseType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_TermUseType_k1, 2};
const bim_type_t t_TermUseType = {t_TermUseType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_TermUseType_top};
