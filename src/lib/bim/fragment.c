/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>

#include "codec.h"
#include "fragment.h"
#include "lib/sys/ioutil.h"
#include "lib/tva/tva_xml.h"

/* urn:tva:metadata:2004 types reachable from the three fragment roots (TS 102 822-3-1 V1.2.1), attributes in BiM coding order */
static const bim_type_t t_TitleType;
static const bim_type_t t_InlineMediaType;
static const bim_type_t t_MediaLocatorType;
static const bim_type_t t_MediaRelTimePointType;
static const bim_type_t t_MediaRelIncrTimePointType;
static const bim_type_t t_ImageLocatorType_BytePosition;
static const bim_type_t t_ImageLocatorType;
static const bim_type_t t_MediaIncrDurationType;
static const bim_type_t t_MediaTimeType;
static const bim_type_t t_TemporalSegmentLocatorType_BytePosition;
static const bim_type_t t_TemporalSegmentLocatorType;
static const bim_type_t t_TitleMediaType;
static const bim_type_t t_ShortTitleType;
static const bim_type_t t_TextualType;
static const bim_type_t t_SynopsisType;
static const bim_type_t t_KeywordType;
static const bim_type_t t_TermNameType;
static const bim_type_t t_ControlledTermType;
static const bim_type_t t_GenreType;
static const bim_type_t t_InlineTermDefinitionType_Name;
static const bim_type_t t_ControlledTermUseType;
static const bim_type_t t_ParentalGuidanceType;
static const bim_type_t t_ExtendedLanguageType;
static const bim_type_t t_CaptionLanguageType;
static const bim_type_t t_SignLanguageType;
static const bim_type_t t_NameComponentType;
static const bim_type_t t_PersonNameType;
static const bim_type_t t_TVAIDRefElementType;
static const bim_type_t t_CreditsItemType;
static const bim_type_t t_CreditsListType;
static const bim_type_t t_AwardType;
static const bim_type_t t_AwardsListItemType;
static const bim_type_t t_AwardsListType;
static const bim_type_t t_SegmentReferenceType;
static const bim_type_t t_RelatedMaterialType;
static const bim_type_t t_TVATimeType;
static const bim_type_t t_CreationCoordinatesType;
static const bim_type_t t_HeaderType;
static const bim_type_t t_TermUseType;
static const bim_type_t t_PlaceType_AdministrativeUnit;
static const bim_type_t t_PlaceType_PostalAddress;
static const bim_type_t t_PlaceType;
static const bim_type_t t_DepictedCoordinatesType;
static const bim_type_t t_ReleaseDateType;
static const bim_type_t t_ReleaseInformationType;
static const bim_type_t t_PurchaseItemType_Price;
static const bim_type_t t_PurchaseItemType_Purchase_QuantityRange;
static const bim_type_t t_PurchaseItemType_Purchase;
static const bim_type_t t_PurchaseItemType;
static const bim_type_t t_PurchaseListType;
static const bim_type_t t_BasicContentDescriptionType;
static const bim_type_t t_UniqueIDType;
static const bim_type_t t_BitRateType;
static const bim_type_t t_AudioLanguageType;
static const bim_type_t t_AudioAttributesType;
static const bim_type_t t_AspectRatioType;
static const bim_type_t t_ColorType;
static const bim_type_t t_VideoAttributesType;
static const bim_type_t t_AVAttributesType;
static const bim_type_t t_CRIDRefType;
static const bim_type_t t_BaseMemberOfType;
static const bim_type_t t_BaseDerivationReasonType;
static const bim_type_t t_DerivedFromType;
static const bim_type_t t_EpisodeOfType;
static const bim_type_t t_AggregationOfType;
static const bim_type_t t_ProgramInformationType;
static const bim_type_t t_InstanceDescriptionType;
static const bim_type_t t_FlagType;
static const bim_type_t t_ScheduleEventType;
static const bim_type_t t_ScheduleType;
static const bim_type_t t_ServiceInformationNameType;
static const bim_type_t t_ValidPeriodType;
static const bim_type_t t_ServiceRefType;
static const bim_type_t t_ServiceInformationType;

static const bim_type_t t_xs_LOCATOR = {NULL, 0, BIM_CONTENT_SIMPLE, BIM_LOCATOR, NULL};
static const bim_type_t t_xs_UNSUPPORTED = {NULL, 0, BIM_CONTENT_SIMPLE, BIM_UNSUPPORTED, NULL};
static const bim_type_t t_xs_UVLU8 = {NULL, 0, BIM_CONTENT_SIMPLE, BIM_UVLU8, NULL};
static const bim_type_t t_xs_STRING = {NULL, 0, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};
static const bim_type_t t_xs_DURATION = {NULL, 0, BIM_CONTENT_SIMPLE, BIM_DURATION, NULL};
static const bim_type_t t_xs_DATETIME = {NULL, 0, BIM_CONTENT_SIMPLE, BIM_DATETIME, NULL};
static const bim_attr_t t_TitleType_attrs[] = {
  {"type", BIM_TERMREF, 0, NULL, 0},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_TitleType = {t_TitleType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_InlineMediaType_attrs[] = {
  {"type", BIM_STRING, 1, NULL, 0},
};
static const bim_part_t t_InlineMediaType_k1[] = {
  {BIM_PART_ELEM, "MediaData16", &t_xs_UNSUPPORTED, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MediaData64", &t_xs_UNSUPPORTED, 1, 1, NULL, 0},
};
static const bim_part_t t_InlineMediaType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 1, 1, t_InlineMediaType_k1, 2},
};
static const bim_part_t t_InlineMediaType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_InlineMediaType_k2, 1};
static const bim_type_t t_InlineMediaType = {t_InlineMediaType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_InlineMediaType_top};

static const bim_part_t t_MediaLocatorType_k1[] = {
  {BIM_PART_ELEM, "MediaUri", &t_xs_LOCATOR, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "InlineMedia", &t_InlineMediaType, 1, 1, NULL, 0},
};
static const bim_part_t t_MediaLocatorType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_MediaLocatorType_k1, 2},
  {BIM_PART_ELEM, "StreamID", &t_xs_UVLU8, 0, 1, NULL, 0},
};
static const bim_part_t t_MediaLocatorType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_MediaLocatorType_k2, 2};
static const bim_type_t t_MediaLocatorType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_MediaLocatorType_top};

static const bim_attr_t t_MediaRelTimePointType_attrs[] = {
  {"mediaTimeBase", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_MediaRelTimePointType = {t_MediaRelTimePointType_attrs, 1, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_MediaRelIncrTimePointType_attrs[] = {
  {"mediaTimeBase", BIM_STRING, 0, NULL, 0},
  {"mediaTimeUnit", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_MediaRelIncrTimePointType = {t_MediaRelIncrTimePointType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_UNSUPPORTED, NULL};

static const bim_attr_t t_ImageLocatorType_BytePosition_attrs[] = {
  {"length", BIM_UVLU8, 0, NULL, 0},
  {"offset", BIM_UVLU8, 1, NULL, 0},
};
static const bim_type_t t_ImageLocatorType_BytePosition = {t_ImageLocatorType_BytePosition_attrs, 2, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_ImageLocatorType_k1[] = {
  {BIM_PART_ELEM, "MediaUri", &t_xs_LOCATOR, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "InlineMedia", &t_InlineMediaType, 1, 1, NULL, 0},
};
static const bim_part_t t_ImageLocatorType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_ImageLocatorType_k1, 2},
  {BIM_PART_ELEM, "StreamID", &t_xs_UVLU8, 0, 1, NULL, 0},
};
static const bim_part_t t_ImageLocatorType_k3[] = {
  {BIM_PART_ELEM, "MediaTimePoint", &t_xs_STRING, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MediaRelTimePoint", &t_MediaRelTimePointType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MediaRelIncrTimePoint", &t_MediaRelIncrTimePointType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "BytePosition", &t_ImageLocatorType_BytePosition, 1, 1, NULL, 0},
};
static const bim_part_t t_ImageLocatorType_k4[] = {
  {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ImageLocatorType_k2, 2},
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_ImageLocatorType_k3, 4},
};
static const bim_part_t t_ImageLocatorType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ImageLocatorType_k4, 2};
static const bim_type_t t_ImageLocatorType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ImageLocatorType_top};

static const bim_attr_t t_MediaIncrDurationType_attrs[] = {
  {"mediaTimeUnit", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_MediaIncrDurationType = {t_MediaIncrDurationType_attrs, 1, BIM_CONTENT_SIMPLE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_MediaTimeType_k1[] = {
  {BIM_PART_ELEM, "MediaTimePoint", &t_xs_STRING, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MediaRelTimePoint", &t_MediaRelTimePointType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MediaRelIncrTimePoint", &t_MediaRelIncrTimePointType, 1, 1, NULL, 0},
};
static const bim_part_t t_MediaTimeType_k2[] = {
  {BIM_PART_ELEM, "MediaDuration", &t_xs_STRING, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MediaIncrDuration", &t_MediaIncrDurationType, 1, 1, NULL, 0},
};
static const bim_part_t t_MediaTimeType_k3[] = {
  {BIM_PART_CHOICE, NULL, NULL, 1, 1, t_MediaTimeType_k1, 3},
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_MediaTimeType_k2, 2},
};
static const bim_part_t t_MediaTimeType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_MediaTimeType_k3, 2};
static const bim_type_t t_MediaTimeType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_MediaTimeType_top};

static const bim_attr_t t_TemporalSegmentLocatorType_BytePosition_attrs[] = {
  {"length", BIM_UVLU8, 0, NULL, 0},
  {"offset", BIM_UVLU8, 1, NULL, 0},
};
static const bim_type_t t_TemporalSegmentLocatorType_BytePosition = {t_TemporalSegmentLocatorType_BytePosition_attrs, 2, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_TemporalSegmentLocatorType_k1[] = {
  {BIM_PART_ELEM, "MediaUri", &t_xs_LOCATOR, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "InlineMedia", &t_InlineMediaType, 1, 1, NULL, 0},
};
static const bim_part_t t_TemporalSegmentLocatorType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_TemporalSegmentLocatorType_k1, 2},
  {BIM_PART_ELEM, "StreamID", &t_xs_UVLU8, 0, 1, NULL, 0},
};
static const bim_part_t t_TemporalSegmentLocatorType_k3[] = {
  {BIM_PART_ELEM, "MediaTime", &t_MediaTimeType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "BytePosition", &t_TemporalSegmentLocatorType_BytePosition, 1, 1, NULL, 0},
};
static const bim_part_t t_TemporalSegmentLocatorType_k4[] = {
  {BIM_PART_SEQ, NULL, NULL, 1, 1, t_TemporalSegmentLocatorType_k2, 2},
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_TemporalSegmentLocatorType_k3, 2},
};
static const bim_part_t t_TemporalSegmentLocatorType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_TemporalSegmentLocatorType_k4, 2};
static const bim_type_t t_TemporalSegmentLocatorType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_TemporalSegmentLocatorType_top};

static const bim_part_t t_TitleMediaType_k1[] = {
  {BIM_PART_ELEM, "TitleImage", &t_ImageLocatorType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "TitleVideo", &t_TemporalSegmentLocatorType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "TitleAudio", &t_TemporalSegmentLocatorType, 0, 1, NULL, 0},
};
static const bim_part_t t_TitleMediaType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_TitleMediaType_k1, 3};
static const bim_type_t t_TitleMediaType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_TitleMediaType_top};

static const bim_attr_t t_ShortTitleType_attrs[] = {
  {"length", BIM_UVLU8, 1, NULL, 0},
  {"type", BIM_TERMREF, 0, NULL, 0},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_ShortTitleType = {t_ShortTitleType_attrs, 3, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_TextualType_attrs[] = {
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_TextualType = {t_TextualType_attrs, 1, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const char *const t_SynopsisType_e0[] = {"short", "medium", "long"};
static const bim_attr_t t_SynopsisType_attrs[] = {
  {"length", BIM_ENUM, 0, t_SynopsisType_e0, 3},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_SynopsisType = {t_SynopsisType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const char *const t_KeywordType_e0[] = {"main", "secondary", "other"};
static const bim_attr_t t_KeywordType_attrs[] = {
  {"type", BIM_ENUM, 0, t_KeywordType_e0, 3},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_KeywordType = {t_KeywordType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_TermNameType_attrs[] = {
  {"preferred", BIM_BOOLEAN, 0, NULL, 0},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_TermNameType = {t_TermNameType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_ControlledTermType_attrs[] = {
  {"href", BIM_TERMREF, 1, NULL, 0},
};
static const bim_part_t t_ControlledTermType_k1[] = {
  {BIM_PART_ELEM, "Name", &t_TermNameType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Definition", &t_TextualType, 0, 1, NULL, 0},
};
static const bim_part_t t_ControlledTermType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ControlledTermType_k1, 2};
static const bim_type_t t_ControlledTermType = {t_ControlledTermType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ControlledTermType_top};

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
static const bim_type_t t_GenreType = {t_GenreType_attrs, 2, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_GenreType_top};

static const bim_attr_t t_InlineTermDefinitionType_Name_attrs[] = {
  {"preferred", BIM_BOOLEAN, 0, NULL, 0},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_InlineTermDefinitionType_Name = {t_InlineTermDefinitionType_Name_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_ControlledTermUseType_attrs[] = {
  {"href", BIM_TERMREF, 1, NULL, 0},
};
static const bim_part_t t_ControlledTermUseType_k1[] = {
  {BIM_PART_ELEM, "Name", &t_InlineTermDefinitionType_Name, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Definition", &t_TextualType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_ControlledTermUseType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ControlledTermUseType_k1, 2};
static const bim_type_t t_ControlledTermUseType = {t_ControlledTermUseType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ControlledTermUseType_top};

static const bim_part_t t_ParentalGuidanceType_k1[] = {
  {BIM_PART_ELEM, "ParentalRating", &t_ControlledTermUseType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "MinimumAge", &t_xs_UVLU8, 1, 1, NULL, 0},
};
static const bim_part_t t_ParentalGuidanceType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 1, 1, t_ParentalGuidanceType_k1, 2},
  {BIM_PART_ELEM, "Region", &t_xs_STRING, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_ParentalGuidanceType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ParentalGuidanceType_k2, 2};
static const bim_type_t t_ParentalGuidanceType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ParentalGuidanceType_top};

static const char *const t_ExtendedLanguageType_e1[] = {"original", "dubbed", "background"};
static const bim_attr_t t_ExtendedLanguageType_attrs[] = {
  {"supplemental", BIM_BOOLEAN, 0, NULL, 0},
  {"type", BIM_ENUM, 0, t_ExtendedLanguageType_e1, 3},
};
static const bim_type_t t_ExtendedLanguageType = {t_ExtendedLanguageType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_CaptionLanguageType_attrs[] = {
  {"closed", BIM_BOOLEAN, 0, NULL, 0},
  {"supplemental", BIM_BOOLEAN, 0, NULL, 0},
};
static const bim_type_t t_CaptionLanguageType = {t_CaptionLanguageType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_SignLanguageType_attrs[] = {
  {"primary", BIM_BOOLEAN, 0, NULL, 0},
  {"translation", BIM_BOOLEAN, 0, NULL, 0},
  {"type", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_SignLanguageType = {t_SignLanguageType_attrs, 3, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_NameComponentType_attrs[] = {
  {"abbrev", BIM_STRING, 0, NULL, 0},
  {"initial", BIM_STRING, 0, NULL, 0},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_NameComponentType = {t_NameComponentType_attrs, 3, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

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
static const bim_type_t t_PersonNameType = {t_PersonNameType_attrs, 4, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_PersonNameType_top};

static const bim_attr_t t_TVAIDRefElementType_attrs[] = {
  {"ref", BIM_STRING, 1, NULL, 0},
};
static const bim_type_t t_TVAIDRefElementType = {t_TVAIDRefElementType_attrs, 1, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

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
static const bim_type_t t_CreditsItemType = {t_CreditsItemType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_CreditsItemType_top};

static const bim_part_t t_CreditsListType_k1[] = {
  {BIM_PART_ELEM, "CreditsItem", &t_CreditsItemType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_CreditsListType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_CreditsListType_k1, 1};
static const bim_type_t t_CreditsListType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_CreditsListType_top};

static const bim_part_t t_AwardType_k1[] = {
  {BIM_PART_ELEM, "Nominee", &t_CreditsItemType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "Recipient", &t_CreditsItemType, 1, 1, NULL, 0},
};
static const bim_part_t t_AwardType_k2[] = {
  {BIM_PART_ELEM, "Category", &t_TextualType, 1, 1, NULL, 0},
  {BIM_PART_CHOICE, NULL, NULL, 0, 1, t_AwardType_k1, 2},
};
static const bim_part_t t_AwardType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_AwardType_k2, 2};
static const bim_type_t t_AwardType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_AwardType_top};

static const bim_part_t t_AwardsListItemType_k1[] = {
  {BIM_PART_ELEM, "Title", &t_TextualType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "Year", &t_xs_UNSUPPORTED, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "Award", &t_AwardType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_AwardsListItemType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_AwardsListItemType_k1, 3};
static const bim_type_t t_AwardsListItemType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_AwardsListItemType_top};

static const bim_part_t t_AwardsListType_k1[] = {
  {BIM_PART_ELEM, "AwardsListItem", &t_AwardsListItemType, 1, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_AwardsListType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_AwardsListType_k1, 1};
static const bim_type_t t_AwardsListType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_AwardsListType_top};

static const char *const t_SegmentReferenceType_e1[] = {"segment", "segmentgroup"};
static const bim_attr_t t_SegmentReferenceType_attrs[] = {
  {"ref", BIM_STRING, 1, NULL, 0},
  {"segmentType", BIM_ENUM, 0, t_SegmentReferenceType_e1, 2},
};
static const bim_type_t t_SegmentReferenceType = {t_SegmentReferenceType_attrs, 2, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

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
static const bim_type_t t_RelatedMaterialType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_RelatedMaterialType_top};

static const bim_part_t t_TVATimeType_k1[] = {
  {BIM_PART_ELEM, "TimePoint", &t_xs_STRING, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "Duration", &t_xs_STRING, 0, 1, NULL, 0},
};
static const bim_part_t t_TVATimeType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_TVATimeType_k1, 2};
static const bim_type_t t_TVATimeType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_TVATimeType_top};

static const bim_part_t t_CreationCoordinatesType_k1[] = {
  {BIM_PART_ELEM, "CreationDate", &t_TVATimeType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "CreationLocation", &t_xs_STRING, 0, 1, NULL, 0},
};
static const bim_part_t t_CreationCoordinatesType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_CreationCoordinatesType_k1, 2};
static const bim_type_t t_CreationCoordinatesType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_CreationCoordinatesType_top};

static const bim_attr_t t_HeaderType_attrs[] = {
  {"id", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_HeaderType = {t_HeaderType_attrs, 1, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_attr_t t_TermUseType_attrs[] = {
  {"href", BIM_TERMREF, 0, NULL, 0},
};
static const bim_part_t t_TermUseType_k1[] = {
  {BIM_PART_ELEM, "Name", &t_InlineTermDefinitionType_Name, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Definition", &t_TextualType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_TermUseType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_TermUseType_k1, 2};
static const bim_type_t t_TermUseType = {t_TermUseType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_TermUseType_top};

static const bim_attr_t t_PlaceType_AdministrativeUnit_attrs[] = {
  {"type", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_PlaceType_AdministrativeUnit = {t_PlaceType_AdministrativeUnit_attrs, 1, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_PlaceType_PostalAddress_attrs[] = {
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_part_t t_PlaceType_PostalAddress_k1[] = {
  {BIM_PART_ELEM, "AddressLine", &t_TextualType, 1, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "PostingIdentifier", &t_TextualType, 0, 1, NULL, 0},
};
static const bim_part_t t_PlaceType_PostalAddress_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_PlaceType_PostalAddress_k1, 2};
static const bim_type_t t_PlaceType_PostalAddress = {t_PlaceType_PostalAddress_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_PlaceType_PostalAddress_top};

static const bim_attr_t t_PlaceType_attrs[] = {
  {"id", BIM_STRING, 0, NULL, 0},
  {"mediaTimeBase", BIM_STRING, 0, NULL, 0},
  {"mediaTimeUnit", BIM_STRING, 0, NULL, 0},
  {"timeBase", BIM_STRING, 0, NULL, 0},
  {"timeUnit", BIM_STRING, 0, NULL, 0},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_part_t t_PlaceType_k1[] = {
  {BIM_PART_ELEM, "Header", &t_HeaderType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_PlaceType_k2[] = {
  {BIM_PART_ELEM, "Name", &t_TextualType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "NameTerm", &t_ControlledTermUseType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Role", &t_TermUseType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Region", &t_xs_STRING, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "AdministrativeUnit", &t_PlaceType_AdministrativeUnit, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "PostalAddress", &t_PlaceType_PostalAddress, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "InternalCoordinates", &t_xs_STRING, 0, 1, NULL, 0},
};
static const bim_part_t t_PlaceType_k3[] = {
  {BIM_PART_SEQ, NULL, NULL, 1, 1, t_PlaceType_k1, 1},
  {BIM_PART_SEQ, NULL, NULL, 1, 1, t_PlaceType_k2, 7},
};
static const bim_part_t t_PlaceType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_PlaceType_k3, 2};
static const bim_type_t t_PlaceType = {t_PlaceType_attrs, 6, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_PlaceType_top};

static const bim_part_t t_DepictedCoordinatesType_k1[] = {
  {BIM_PART_ELEM, "DepictedDate", &t_TVATimeType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "DepictedLocation", &t_PlaceType, 0, 1, NULL, 0},
};
static const bim_part_t t_DepictedCoordinatesType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_DepictedCoordinatesType_k1, 2};
static const bim_type_t t_DepictedCoordinatesType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_DepictedCoordinatesType_top};

static const bim_part_t t_ReleaseDateType_k1[] = {
  {BIM_PART_ELEM, "DayAndYear", &t_xs_UNSUPPORTED, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "Year", &t_xs_UNSUPPORTED, 1, 1, NULL, 0},
};
static const bim_part_t t_ReleaseDateType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 1, 1, t_ReleaseDateType_k1, 2},
};
static const bim_part_t t_ReleaseDateType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ReleaseDateType_k2, 1};
static const bim_type_t t_ReleaseDateType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ReleaseDateType_top};

static const bim_part_t t_ReleaseInformationType_k1[] = {
  {BIM_PART_ELEM, "ReleaseDate", &t_ReleaseDateType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "ReleaseLocation", &t_xs_STRING, 0, 1, NULL, 0},
};
static const bim_part_t t_ReleaseInformationType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ReleaseInformationType_k1, 2};
static const bim_type_t t_ReleaseInformationType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ReleaseInformationType_top};

static const bim_attr_t t_PurchaseItemType_Price_attrs[] = {
  {"currency", BIM_STRING, 1, NULL, 0},
};
static const bim_type_t t_PurchaseItemType_Price = {t_PurchaseItemType_Price_attrs, 1, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_PurchaseItemType_Purchase_QuantityRange_attrs[] = {
  {"max", BIM_UNSUPPORTED, 0, NULL, 0},
  {"min", BIM_UNSUPPORTED, 0, NULL, 0},
};
static const bim_type_t t_PurchaseItemType_Purchase_QuantityRange = {t_PurchaseItemType_Purchase_QuantityRange_attrs, 2, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_PurchaseItemType_Purchase_k1[] = {
  {BIM_PART_ELEM, "PurchaseType", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "QuantityUnit", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "QuantityRange", &t_PurchaseItemType_Purchase_QuantityRange, 0, 1, NULL, 0},
};
static const bim_part_t t_PurchaseItemType_Purchase_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_PurchaseItemType_Purchase_k1, 3};
static const bim_type_t t_PurchaseItemType_Purchase = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_PurchaseItemType_Purchase_top};

static const bim_attr_t t_PurchaseItemType_attrs[] = {
  {"end", BIM_DATETIME, 0, NULL, 0},
  {"start", BIM_DATETIME, 0, NULL, 0},
};
static const bim_part_t t_PurchaseItemType_k1[] = {
  {BIM_PART_ELEM, "Price", &t_PurchaseItemType_Price, 1, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Purchase", &t_PurchaseItemType_Purchase, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Description", &t_TextualType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "PricingServerURL", &t_xs_LOCATOR, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_PurchaseItemType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_PurchaseItemType_k1, 4};
static const bim_type_t t_PurchaseItemType = {t_PurchaseItemType_attrs, 2, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_PurchaseItemType_top};

static const bim_part_t t_PurchaseListType_k1[] = {
  {BIM_PART_ELEM, "PurchaseItem", &t_PurchaseItemType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "PurchaseIdRef", &t_xs_STRING, 1, 1, NULL, 0},
};
static const bim_part_t t_PurchaseListType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 0, BIM_UNBOUNDED, t_PurchaseListType_k1, 2},
};
static const bim_part_t t_PurchaseListType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_PurchaseListType_k2, 1};
static const bim_type_t t_PurchaseListType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_PurchaseListType_top};

static const bim_part_t t_BasicContentDescriptionType_k1[] = {
  {BIM_PART_ELEM, "Title", &t_TitleType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "MediaTitle", &t_TitleMediaType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ShortTitle", &t_ShortTitleType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Synopsis", &t_SynopsisType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "PromotionalInformation", &t_TextualType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Keyword", &t_KeywordType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Genre", &t_GenreType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ParentalGuidance", &t_ParentalGuidanceType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Language", &t_ExtendedLanguageType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "CaptionLanguage", &t_CaptionLanguageType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "SignLanguage", &t_SignLanguageType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "CreditsList", &t_CreditsListType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "AwardsList", &t_AwardsListType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "RelatedMaterial", &t_RelatedMaterialType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ProductionDate", &t_TVATimeType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "ProductionLocation", &t_xs_STRING, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "CreationCoordinates", &t_CreationCoordinatesType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "DepictedCoordinates", &t_DepictedCoordinatesType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ReleaseInformation", &t_ReleaseInformationType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Duration", &t_xs_DURATION, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "PurchaseList", &t_PurchaseListType, 0, 1, NULL, 0},
};
static const bim_part_t t_BasicContentDescriptionType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_BasicContentDescriptionType_k1, 21};
static const bim_type_t t_BasicContentDescriptionType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_BasicContentDescriptionType_top};

static const char *const t_UniqueIDType_e1[] = {"text", "base16", "base64"};
static const bim_attr_t t_UniqueIDType_attrs[] = {
  {"authority", BIM_STRING, 0, NULL, 0},
  {"encoding", BIM_ENUM, 0, t_UniqueIDType_e1, 3},
  {"organization", BIM_STRING, 0, NULL, 0},
  {"type", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_UniqueIDType = {t_UniqueIDType_attrs, 4, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_BitRateType_attrs[] = {
  {"average", BIM_UVLU8, 0, NULL, 0},
  {"maximum", BIM_UVLU8, 0, NULL, 0},
  {"minimum", BIM_UVLU8, 0, NULL, 0},
  {"variable", BIM_BOOLEAN, 0, NULL, 0},
};
static const bim_type_t t_BitRateType = {t_BitRateType_attrs, 4, BIM_CONTENT_SIMPLE, BIM_UVLU8, NULL};

static const char *const t_AudioLanguageType_e2[] = {"original", "dubbed", "background"};
static const bim_attr_t t_AudioLanguageType_attrs[] = {
  {"purpose", BIM_TERMREF, 0, NULL, 0},
  {"supplemental", BIM_BOOLEAN, 0, NULL, 0},
  {"type", BIM_ENUM, 0, t_AudioLanguageType_e2, 3},
};
static const bim_type_t t_AudioLanguageType = {t_AudioLanguageType_attrs, 3, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_part_t t_AudioAttributesType_k1[] = {
  {BIM_PART_ELEM, "Coding", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "NumOfChannels", &t_xs_UNSUPPORTED, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "MixType", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "AudioLanguage", &t_AudioLanguageType, 0, 1, NULL, 0},
};
static const bim_part_t t_AudioAttributesType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_AudioAttributesType_k1, 4};
static const bim_type_t t_AudioAttributesType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_AudioAttributesType_top};

static const char *const t_AspectRatioType_e0[] = {"original", "publication"};
static const bim_attr_t t_AspectRatioType_attrs[] = {
  {"type", BIM_ENUM, 0, t_AspectRatioType_e0, 2},
};
static const bim_type_t t_AspectRatioType = {t_AspectRatioType_attrs, 1, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const char *const t_ColorType_e0[] = {"color", "blackAndWhite", "blackAndWhiteAndColor", "colorized"};
static const bim_attr_t t_ColorType_attrs[] = {
  {"type", BIM_ENUM, 1, t_ColorType_e0, 4},
};
static const bim_type_t t_ColorType = {t_ColorType_attrs, 1, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_VideoAttributesType_k1[] = {
  {BIM_PART_ELEM, "Coding", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Scan", NULL, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "HorizontalSize", &t_xs_UNSUPPORTED, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "VerticalSize", &t_xs_UNSUPPORTED, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "AspectRatio", &t_AspectRatioType, 0, 2, NULL, 0},
  {BIM_PART_ELEM, "Color", &t_ColorType, 0, 1, NULL, 0},
};
static const bim_part_t t_VideoAttributesType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_VideoAttributesType_k1, 6};
static const bim_type_t t_VideoAttributesType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_VideoAttributesType_top};

static const bim_part_t t_AVAttributesType_k1[] = {
  {BIM_PART_ELEM, "FileFormat", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "FileSize", &t_xs_UVLU8, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "System", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "BitRate", &t_BitRateType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "AudioAttributes", &t_AudioAttributesType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "VideoAttributes", &t_VideoAttributesType, 0, 1, NULL, 0},
};
static const bim_part_t t_AVAttributesType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_AVAttributesType_k1, 6};
static const bim_type_t t_AVAttributesType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_AVAttributesType_top};

static const bim_attr_t t_CRIDRefType_attrs[] = {
  {"crid", BIM_LOCATOR, 1, NULL, 0},
};
static const bim_type_t t_CRIDRefType = {t_CRIDRefType_attrs, 1, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_attr_t t_BaseMemberOfType_attrs[] = {
  {"crid", BIM_LOCATOR, 1, NULL, 0},
  {"index", BIM_UNSUPPORTED, 0, NULL, 0},
};
static const bim_type_t t_BaseMemberOfType = {t_BaseMemberOfType_attrs, 2, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_type_t t_BaseDerivationReasonType = {NULL, 0, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_attr_t t_DerivedFromType_attrs[] = {
  {"crid", BIM_LOCATOR, 1, NULL, 0},
  {"index", BIM_UNSUPPORTED, 0, NULL, 0},
};
static const bim_part_t t_DerivedFromType_k1[] = {
  {BIM_PART_ELEM, "DerivationReason", &t_BaseDerivationReasonType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_DerivedFromType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_DerivedFromType_k1, 1};
static const bim_type_t t_DerivedFromType = {t_DerivedFromType_attrs, 2, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_DerivedFromType_top};

static const bim_attr_t t_EpisodeOfType_attrs[] = {
  {"crid", BIM_LOCATOR, 1, NULL, 0},
  {"index", BIM_UNSUPPORTED, 0, NULL, 0},
};
static const bim_type_t t_EpisodeOfType = {t_EpisodeOfType_attrs, 2, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const char *const t_AggregationOfType_e0[] = {"omnibus", "magazine"};
static const bim_attr_t t_AggregationOfType_attrs[] = {
  {"type", BIM_ENUM, 1, t_AggregationOfType_e0, 2},
};
static const bim_part_t t_AggregationOfType_k1[] = {
  {BIM_PART_ELEM, "AggregatedProgram", &t_CRIDRefType, 2, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_AggregationOfType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_AggregationOfType_k1, 1};
static const bim_type_t t_AggregationOfType = {t_AggregationOfType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_AggregationOfType_top};

static const bim_attr_t t_ProgramInformationType_attrs[] = {
  {"fragmentId", BIM_STRING, 0, NULL, 0},
  {"fragmentVersion", BIM_UVLU8, 0, NULL, 0},
  {"programId", BIM_LOCATOR, 1, NULL, 0},
};
static const bim_part_t t_ProgramInformationType_k1[] = {
  {BIM_PART_ELEM, "BasicDescription", &t_BasicContentDescriptionType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "OtherIdentifier", &t_UniqueIDType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "AVAttributes", &t_AVAttributesType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "MemberOf", &t_BaseMemberOfType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "DerivedFrom", &t_DerivedFromType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "EpisodeOf", &t_EpisodeOfType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "PartOfAggregatedProgram", &t_xs_LOCATOR, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "AggregationOf", &t_AggregationOfType, 0, 1, NULL, 0},
};
static const bim_part_t t_ProgramInformationType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ProgramInformationType_k1, 8};
static const bim_type_t t_ProgramInformationType = {t_ProgramInformationType_attrs, 3, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ProgramInformationType_top};

static const bim_part_t t_InstanceDescriptionType_k1[] = {
  {BIM_PART_ELEM, "Title", &t_TitleType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Synopsis", &t_SynopsisType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Genre", &t_GenreType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "PurchaseList", &t_PurchaseListType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "CaptionLanguage", &t_CaptionLanguageType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "SignLanguage", &t_SignLanguageType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ParentalGuidance", &t_ParentalGuidanceType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "AVAttributes", &t_AVAttributesType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "MemberOf", &t_BaseMemberOfType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "OtherIdentifier", &t_UniqueIDType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "RelatedMaterial", &t_RelatedMaterialType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_InstanceDescriptionType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_InstanceDescriptionType_k1, 11};
static const bim_type_t t_InstanceDescriptionType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_InstanceDescriptionType_top};

static const bim_attr_t t_FlagType_attrs[] = {
  {"value", BIM_BOOLEAN, 1, NULL, 0},
};
static const bim_type_t t_FlagType = {t_FlagType_attrs, 1, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_ScheduleEventType_k1[] = {
  {BIM_PART_ELEM, "Program", &t_CRIDRefType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "ProgramURL", &t_xs_LOCATOR, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "InstanceMetadataId", &t_xs_LOCATOR, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "InstanceDescription", &t_InstanceDescriptionType, 0, 1, NULL, 0},
};
static const bim_part_t t_ScheduleEventType_k2[] = {
  {BIM_PART_ELEM, "PublishedStartTime", &t_xs_DATETIME, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "PublishedEndTime", &t_xs_DATETIME, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "PublishedDuration", &t_xs_DURATION, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Live", &t_FlagType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Repeat", &t_FlagType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "FirstShowing", &t_FlagType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "LastShowing", &t_FlagType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Free", &t_FlagType, 0, 1, NULL, 0},
};
static const bim_part_t t_ScheduleEventType_k3[] = {
  {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ScheduleEventType_k1, 4},
  {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ScheduleEventType_k2, 8},
};
static const bim_part_t t_ScheduleEventType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ScheduleEventType_k3, 2};
static const bim_type_t t_ScheduleEventType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ScheduleEventType_top};

static const bim_attr_t t_ScheduleType_attrs[] = {
  {"end", BIM_DATETIME, 0, NULL, 0},
  {"fragmentId", BIM_STRING, 0, NULL, 0},
  {"fragmentVersion", BIM_UVLU8, 0, NULL, 0},
  {"serviceIDRef", BIM_STRING, 1, NULL, 0},
  {"start", BIM_DATETIME, 0, NULL, 0},
};
static const bim_part_t t_ScheduleType_k1[] = {
  {BIM_PART_ELEM, "ScheduleEvent", &t_ScheduleEventType, 1, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_ScheduleType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ScheduleType_k1, 1};
static const bim_type_t t_ScheduleType = {t_ScheduleType_attrs, 5, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ScheduleType_top};

static const char *const t_ServiceInformationNameType_e0[] = {"short", "medium", "long"};
static const bim_attr_t t_ServiceInformationNameType_attrs[] = {
  {"length", BIM_ENUM, 0, t_ServiceInformationNameType_e0, 3},
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_type_t t_ServiceInformationNameType = {t_ServiceInformationNameType_attrs, 2, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_part_t t_ValidPeriodType_k1[] = {
  {BIM_PART_ELEM, "ValidFrom", &t_xs_DATETIME, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "ValidTo", &t_xs_DATETIME, 0, 1, NULL, 0},
};
static const bim_part_t t_ValidPeriodType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ValidPeriodType_k1, 2};
static const bim_type_t t_ValidPeriodType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ValidPeriodType_top};

static const bim_attr_t t_ServiceRefType_attrs[] = {
  {"serviceIDRef", BIM_STRING, 1, NULL, 0},
};
static const bim_part_t t_ServiceRefType_k1[] = {
  {BIM_PART_ELEM, "ValidPeriod", &t_ValidPeriodType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_ServiceRefType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ServiceRefType_k1, 1};
static const bim_type_t t_ServiceRefType = {t_ServiceRefType_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ServiceRefType_top};

static const bim_attr_t t_ServiceInformationType_attrs[] = {
  {"fragmentId", BIM_STRING, 0, NULL, 0},
  {"fragmentVersion", BIM_UVLU8, 0, NULL, 0},
  {"serviceId", BIM_STRING, 1, NULL, 0},
};
static const bim_part_t t_ServiceInformationType_k1[] = {
  {BIM_PART_ELEM, "Name", &t_ServiceInformationNameType, 1, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "Owner", &t_xs_STRING, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "ServiceURL", &t_xs_LOCATOR, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Logo", &t_MediaLocatorType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ServiceDescription", &t_SynopsisType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ServiceGenre", &t_GenreType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ServiceLanguage", &t_xs_STRING, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "ParentService", &t_ServiceRefType, 0, BIM_UNBOUNDED, NULL, 0},
};
static const bim_part_t t_ServiceInformationType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ServiceInformationType_k1, 8};
static const bim_type_t t_ServiceInformationType = {t_ServiceInformationType_attrs, 3, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ServiceInformationType_top};


const bim_type_t *const tva_type_program_information = &t_ProgramInformationType;
const bim_type_t *const tva_type_schedule = &t_ScheduleType;
const bim_type_t *const tva_type_service_information = &t_ServiceInformationType;

static bim_node_t *add_leaf(bim_node_t *parent, const char *name, const char *text) { return bim_node_add_new(parent, name, text); }

static int encode_tree(const bim_type_t *t, bim_node_t *root, bitwriter_t *bw, strrepo_writer_t *sw) {
  int rc = root ? bim_encode_payload(t, root, bw, sw) : -1;
  bim_node_free(root);
  return rc;
}

int fragment_encode_program_information(const bcg_programme_t *pr, bitwriter_t *bw, strrepo_writer_t *sw) {
  char buf[TVA_CRID_BUF];
  bim_node_t *root = bim_node_new("ProgramInformation");
  bim_node_t *bd;
  if (!root || bim_node_set_attr(root, "programId", tva_programme_crid(pr, buf, sizeof buf))) goto fail;
  bd = add_leaf(root, "BasicDescription", NULL);
  if (!bd) goto fail;
  if (pr->title && !add_leaf(bd, "Title", pr->title)) goto fail;
  if (pr->desc && !add_leaf(bd, "Synopsis", pr->desc)) goto fail;
  if (pr->category) {
    bim_node_t *g = add_leaf(bd, "Genre", NULL);
    if (!g || bim_node_set_attr(g, "href", tva_genre_href(pr->category)) || !add_leaf(g, "Name", pr->category)) goto fail;
  }
  return encode_tree(tva_type_program_information, root, bw, sw);
fail:
  bim_node_free(root);
  return -1;
}

int fragment_decode_program_information(bitreader_t *br, strrepo_reader_t *sr, char *crid_out, size_t crid_cap, bcg_programme_t *pr_out) {
  bim_node_t *root;
  const bim_node_t *bd;
  const char *id;
  memset(pr_out, 0, sizeof *pr_out);
  if (bim_decode_payload(tva_type_program_information, "ProgramInformation", br, sr, &root)) return -1;
  id = bim_node_attr(root, "programId");
  bd = bim_node_child(root, "BasicDescription");
  if (!id || !bd || strlen(id) >= crid_cap) goto fail;
  memcpy(crid_out, id, strlen(id) + 1);
  for (int i = 0; i < bd->nkids; i++) {
    const bim_node_t *k = bd->kids[i];
    if (!strcmp(k->name, "Title") && !pr_out->title) {
      if (bcg_set_text(&pr_out->title, k->text)) goto fail;
    } else if (!strcmp(k->name, "Synopsis") && !pr_out->desc) {
      if (bcg_set_text(&pr_out->desc, k->text)) goto fail;
    } else if (!strcmp(k->name, "Genre") && !pr_out->category) {
      const bim_node_t *name = bim_node_child(k, "Name");
      if (name && bcg_set_text(&pr_out->category, name->text)) goto fail;
    }
  }
  bim_node_free(root);
  return 0;
fail:
  bcg_programme_free_text(pr_out);
  bim_node_free(root);
  return -1;
}

int fragment_encode_schedule(const char *channel_id, const bcg_programme_t *programmes, int first, const int *next, bitwriter_t *bw, strrepo_writer_t *sw) {
  bim_node_t *root = bim_node_new("Schedule");
  if (!root || bim_node_set_attr(root, "serviceIDRef", channel_id)) goto fail;
  for (int j = first; j != -1; j = next[j]) {
    const bcg_programme_t *pr = &programmes[j];
    char buf[TVA_CRID_BUF];
    bim_node_t *ev = add_leaf(root, "ScheduleEvent", NULL);
    bim_node_t *p = ev ? add_leaf(ev, "Program", NULL) : NULL;
    if (!p || bim_node_set_attr(p, "crid", tva_programme_crid(pr, buf, sizeof buf))) goto fail;
    if (!add_leaf(ev, "PublishedStartTime", pr->start)) goto fail;
    if (pr->stop[0] && !add_leaf(ev, "PublishedEndTime", pr->stop)) goto fail;
  }
  return encode_tree(tva_type_schedule, root, bw, sw);
fail:
  bim_node_free(root);
  return -1;
}

int fragment_decode_schedule(bitreader_t *br, strrepo_reader_t *sr, bcg_doc_t *doc, fragment_text_lookup_fn lookup, void *ctx) {
  bim_node_t *root;
  const char *channel;
  if (bim_decode_payload(tva_type_schedule, "Schedule", br, sr, &root)) return -1;
  channel = bim_node_attr(root, "serviceIDRef");
  if (!channel || strlen(channel) >= BCG_ID_LEN) goto fail;
  for (int i = 0; i < root->nkids; i++) {
    const bim_node_t *ev = root->kids[i];
    const bim_node_t *prog = bim_node_child(ev, "Program");
    const bim_node_t *start = bim_node_child(ev, "PublishedStartTime");
    const bim_node_t *stop = bim_node_child(ev, "PublishedEndTime");
    const char *crid = prog ? bim_node_attr(prog, "crid") : NULL;
    bcg_programme_t *pr;
    if (!crid || !start || !start->text || strlen(start->text) >= BCG_TIME_LEN) continue;
    pr = bcg_add_programme(doc);
    if (!pr) goto fail;
    bufcpy(pr->channel_id, sizeof pr->channel_id, channel);
    bufcpy(pr->start, sizeof pr->start, start->text);
    if (stop && stop->text && strlen(stop->text) < BCG_TIME_LEN) bufcpy(pr->stop, sizeof pr->stop, stop->text);
    if (lookup && lookup(ctx, crid, pr) == -2) goto fail;
  }
  bim_node_free(root);
  return 0;
fail:
  bim_node_free(root);
  return -1;
}

/* two strings after the fragment's own: dvb:// triplet ("" if none), "1" if Name was synthesized.
   receivers find strings via the fragment reference and never see them */
int fragment_encode_service_information(const bcg_channel_t *c, bitwriter_t *bw, strrepo_writer_t *sw) {
  char dtt[64];
  sbuf_t db;
  bim_node_t *root = bim_node_new("ServiceInformation");
  if (!root || bim_node_set_attr(root, "serviceId", c->id)) goto fail;
  if (c->name_count == 0 && !add_leaf(root, "Name", c->id)) goto fail;
  for (int j = 0; j < c->name_count; j++)
    if (!add_leaf(root, "Name", c->names[j])) goto fail;
  if (!add_leaf(root, "ServiceURL", c->uri)) goto fail;
  if (encode_tree(tva_type_service_information, root, bw, sw)) return -1;
  sbuf_init(&db, dtt, sizeof dtt);
  if (c->onid || c->tsid || c->sid) {
    sbuf_add(&db, "dvb://");
    sbuf_add_hex(&db, c->onid);
    sbuf_add(&db, ".");
    sbuf_add_hex(&db, c->tsid);
    sbuf_add(&db, ".");
    sbuf_add_hex(&db, c->sid);
  }
  if (dvb_string_encode(sw, dtt)) return -1;
  return dvb_string_encode(sw, c->name_count == 0 ? "1" : "0");
fail:
  bim_node_free(root);
  return -1;
}

int fragment_decode_service_information(bitreader_t *br, strrepo_reader_t *sr, bcg_channel_t *c_out) {
  bim_node_t *root;
  const char *id;
  const bim_node_t *url;
  char dtt[64];
  char synth[8];
  unsigned onid;
  unsigned tsid;
  unsigned sid;
  int no_names = 0;
  int dropped = 0;
  size_t tail_pos;
  memset(c_out, 0, sizeof *c_out);
  if (bim_decode_payload(tva_type_service_information, "ServiceInformation", br, sr, &root)) return -1;
  id = bim_node_attr(root, "serviceId");
  if (!id || strlen(id) >= sizeof c_out->id) goto fail;
  memcpy(c_out->id, id, strlen(id) + 1);
  url = bim_node_child(root, "ServiceURL");
  if (url && url->text && strlen(url->text) < sizeof c_out->uri) memcpy(c_out->uri, url->text, strlen(url->text) + 1);
  tail_pos = sr->pos;
  if (!dvb_string_decode(sr, dtt, sizeof dtt) && !dvb_string_decode(sr, synth, sizeof synth) && (!strcmp(synth, "0") || !strcmp(synth, "1")) &&
      (!dtt[0] || !strncmp(dtt, "dvb://", 6))) {
    no_names = synth[0] == '1';
    if (sscanf(dtt, "dvb://%x.%x.%x", &onid, &tsid, &sid) == 3) {
      c_out->onid = onid;
      c_out->tsid = tsid;
      c_out->sid = sid;
    }
  } else {
    sr->pos = tail_pos; /* foreign: strings belong to next fragment */
  }
  for (int i = 0; !no_names && i < root->nkids; i++) {
    if (strcmp(root->kids[i]->name, "Name") || !root->kids[i]->text) continue;
    if (strlen(root->kids[i]->text) >= BCG_ID_LEN) {
      dropped++;
    } else if (bcg_channel_add_name(c_out, root->kids[i]->text)) {
      dropped++;
    }
  }
  if (dropped) log_code("LW400234", "bcg: channel %s: %d <Name> dropped (max %d names, %d bytes each)", c_out->id, dropped, BCG_MAX_NAMES, BCG_ID_LEN - 1);
  bim_node_free(root);
  return 0;
fail:
  bim_node_free(root);
  return -1;
}
