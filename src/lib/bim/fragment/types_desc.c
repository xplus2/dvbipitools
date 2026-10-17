/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/bim/fragment/fragment_int.h"

static const bim_attr_t t_PlaceType_AdministrativeUnit_attrs[] = {
  {"type", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_PlaceType_AdministrativeUnit = {t_PlaceType_AdministrativeUnit_attrs, 1, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_PlaceType_PostalAddress_attrs[] = {
  {"xml:lang", BIM_STRING, 0, NULL, 0},
};
static const bim_part_t t_PlaceType_PostalAddress_k1[] = {
  {BIM_PART_ELEM, "AddressLine", &t_TextualType, 1, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "PostingIdentifier", &t_TextualType, 0, 1, NULL, 0},
};
static const bim_part_t t_PlaceType_PostalAddress_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_PlaceType_PostalAddress_k1, 2};
const bim_type_t t_PlaceType_PostalAddress = {t_PlaceType_PostalAddress_attrs, 1, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_PlaceType_PostalAddress_top};

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
const bim_type_t t_PlaceType = {t_PlaceType_attrs, 6, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_PlaceType_top};

static const bim_part_t t_DepictedCoordinatesType_k1[] = {
  {BIM_PART_ELEM, "DepictedDate", &t_TVATimeType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "DepictedLocation", &t_PlaceType, 0, 1, NULL, 0},
};
static const bim_part_t t_DepictedCoordinatesType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_DepictedCoordinatesType_k1, 2};
const bim_type_t t_DepictedCoordinatesType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_DepictedCoordinatesType_top};

static const bim_part_t t_ReleaseDateType_k1[] = {
  {BIM_PART_ELEM, "DayAndYear", &t_xs_UNSUPPORTED, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "Year", &t_xs_UNSUPPORTED, 1, 1, NULL, 0},
};
static const bim_part_t t_ReleaseDateType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 1, 1, t_ReleaseDateType_k1, 2},
};
static const bim_part_t t_ReleaseDateType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ReleaseDateType_k2, 1};
const bim_type_t t_ReleaseDateType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ReleaseDateType_top};

static const bim_part_t t_ReleaseInformationType_k1[] = {
  {BIM_PART_ELEM, "ReleaseDate", &t_ReleaseDateType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "ReleaseLocation", &t_xs_STRING, 0, 1, NULL, 0},
};
static const bim_part_t t_ReleaseInformationType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_ReleaseInformationType_k1, 2};
const bim_type_t t_ReleaseInformationType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_ReleaseInformationType_top};

static const bim_attr_t t_PurchaseItemType_Price_attrs[] = {
  {"currency", BIM_STRING, 1, NULL, 0},
};
const bim_type_t t_PurchaseItemType_Price = {t_PurchaseItemType_Price_attrs, 1, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_PurchaseItemType_Purchase_QuantityRange_attrs[] = {
  {"max", BIM_UNSUPPORTED, 0, NULL, 0},
  {"min", BIM_UNSUPPORTED, 0, NULL, 0},
};
const bim_type_t t_PurchaseItemType_Purchase_QuantityRange = {t_PurchaseItemType_Purchase_QuantityRange_attrs, 2, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_PurchaseItemType_Purchase_k1[] = {
  {BIM_PART_ELEM, "PurchaseType", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "QuantityUnit", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "QuantityRange", &t_PurchaseItemType_Purchase_QuantityRange, 0, 1, NULL, 0},
};
static const bim_part_t t_PurchaseItemType_Purchase_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_PurchaseItemType_Purchase_k1, 3};
const bim_type_t t_PurchaseItemType_Purchase = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_PurchaseItemType_Purchase_top};

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
const bim_type_t t_PurchaseItemType = {t_PurchaseItemType_attrs, 2, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_PurchaseItemType_top};

static const bim_part_t t_PurchaseListType_k1[] = {
  {BIM_PART_ELEM, "PurchaseItem", &t_PurchaseItemType, 1, 1, NULL, 0},
  {BIM_PART_ELEM, "PurchaseIdRef", &t_xs_STRING, 1, 1, NULL, 0},
};
static const bim_part_t t_PurchaseListType_k2[] = {
  {BIM_PART_CHOICE, NULL, NULL, 0, BIM_UNBOUNDED, t_PurchaseListType_k1, 2},
};
static const bim_part_t t_PurchaseListType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_PurchaseListType_k2, 1};
const bim_type_t t_PurchaseListType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_PurchaseListType_top};

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
const bim_type_t t_BasicContentDescriptionType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_BasicContentDescriptionType_top};

static const char *const t_UniqueIDType_e1[] = {"text", "base16", "base64"};
static const bim_attr_t t_UniqueIDType_attrs[] = {
  {"authority", BIM_STRING, 0, NULL, 0},
  {"encoding", BIM_ENUM, 0, t_UniqueIDType_e1, 3},
  {"organization", BIM_STRING, 0, NULL, 0},
  {"type", BIM_STRING, 0, NULL, 0},
};
const bim_type_t t_UniqueIDType = {t_UniqueIDType_attrs, 4, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_attr_t t_BitRateType_attrs[] = {
  {"average", BIM_UVLU8, 0, NULL, 0},
  {"maximum", BIM_UVLU8, 0, NULL, 0},
  {"minimum", BIM_UVLU8, 0, NULL, 0},
  {"variable", BIM_BOOLEAN, 0, NULL, 0},
};
const bim_type_t t_BitRateType = {t_BitRateType_attrs, 4, BIM_CONTENT_SIMPLE, BIM_UVLU8, NULL};

static const char *const t_AudioLanguageType_e2[] = {"original", "dubbed", "background"};
static const bim_attr_t t_AudioLanguageType_attrs[] = {
  {"purpose", BIM_TERMREF, 0, NULL, 0},
  {"supplemental", BIM_BOOLEAN, 0, NULL, 0},
  {"type", BIM_ENUM, 0, t_AudioLanguageType_e2, 3},
};
const bim_type_t t_AudioLanguageType = {t_AudioLanguageType_attrs, 3, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const bim_part_t t_AudioAttributesType_k1[] = {
  {BIM_PART_ELEM, "Coding", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "NumOfChannels", &t_xs_UNSUPPORTED, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "MixType", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "AudioLanguage", &t_AudioLanguageType, 0, 1, NULL, 0},
};
static const bim_part_t t_AudioAttributesType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_AudioAttributesType_k1, 4};
const bim_type_t t_AudioAttributesType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_AudioAttributesType_top};

static const char *const t_AspectRatioType_e0[] = {"original", "publication"};
static const bim_attr_t t_AspectRatioType_attrs[] = {
  {"type", BIM_ENUM, 0, t_AspectRatioType_e0, 2},
};
const bim_type_t t_AspectRatioType = {t_AspectRatioType_attrs, 1, BIM_CONTENT_SIMPLE, BIM_STRING, NULL};

static const char *const t_ColorType_e0[] = {"color", "blackAndWhite", "blackAndWhiteAndColor", "colorized"};
static const bim_attr_t t_ColorType_attrs[] = {
  {"type", BIM_ENUM, 1, t_ColorType_e0, 4},
};
const bim_type_t t_ColorType = {t_ColorType_attrs, 1, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};

static const bim_part_t t_VideoAttributesType_k1[] = {
  {BIM_PART_ELEM, "Coding", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "Scan", NULL, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "HorizontalSize", &t_xs_UNSUPPORTED, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "VerticalSize", &t_xs_UNSUPPORTED, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "AspectRatio", &t_AspectRatioType, 0, 2, NULL, 0},
  {BIM_PART_ELEM, "Color", &t_ColorType, 0, 1, NULL, 0},
};
static const bim_part_t t_VideoAttributesType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_VideoAttributesType_k1, 6};
const bim_type_t t_VideoAttributesType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_VideoAttributesType_top};

static const bim_part_t t_AVAttributesType_k1[] = {
  {BIM_PART_ELEM, "FileFormat", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "FileSize", &t_xs_UVLU8, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "System", &t_ControlledTermType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "BitRate", &t_BitRateType, 0, 1, NULL, 0},
  {BIM_PART_ELEM, "AudioAttributes", &t_AudioAttributesType, 0, BIM_UNBOUNDED, NULL, 0},
  {BIM_PART_ELEM, "VideoAttributes", &t_VideoAttributesType, 0, 1, NULL, 0},
};
static const bim_part_t t_AVAttributesType_top = {BIM_PART_SEQ, NULL, NULL, 1, 1, t_AVAttributesType_k1, 6};
const bim_type_t t_AVAttributesType = {NULL, 0, BIM_CONTENT_SEQ, BIM_UNSUPPORTED, &t_AVAttributesType_top};

static const bim_attr_t t_CRIDRefType_attrs[] = {
  {"crid", BIM_LOCATOR, 1, NULL, 0},
};
const bim_type_t t_CRIDRefType = {t_CRIDRefType_attrs, 1, BIM_CONTENT_NONE, BIM_UNSUPPORTED, NULL};
