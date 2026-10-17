/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef BIM_FRAGMENT_INT_H
#define BIM_FRAGMENT_INT_H

#include "lib/bim/codec.h"

/* urn:tva:metadata:2004 types reachable from the three fragment roots (TS 102 822-3-1 V1.2.1), attributes in BiM coding order */
extern const bim_type_t t_TitleType;
extern const bim_type_t t_InlineMediaType;
extern const bim_type_t t_MediaLocatorType;
extern const bim_type_t t_MediaRelTimePointType;
extern const bim_type_t t_MediaRelIncrTimePointType;
extern const bim_type_t t_ImageLocatorType_BytePosition;
extern const bim_type_t t_ImageLocatorType;
extern const bim_type_t t_MediaIncrDurationType;
extern const bim_type_t t_MediaTimeType;
extern const bim_type_t t_TemporalSegmentLocatorType_BytePosition;
extern const bim_type_t t_TemporalSegmentLocatorType;
extern const bim_type_t t_TitleMediaType;
extern const bim_type_t t_ShortTitleType;
extern const bim_type_t t_TextualType;
extern const bim_type_t t_SynopsisType;
extern const bim_type_t t_KeywordType;
extern const bim_type_t t_TermNameType;
extern const bim_type_t t_ControlledTermType;
extern const bim_type_t t_GenreType;
extern const bim_type_t t_InlineTermDefinitionType_Name;
extern const bim_type_t t_ControlledTermUseType;
extern const bim_type_t t_ParentalGuidanceType;
extern const bim_type_t t_ExtendedLanguageType;
extern const bim_type_t t_CaptionLanguageType;
extern const bim_type_t t_SignLanguageType;
extern const bim_type_t t_NameComponentType;
extern const bim_type_t t_PersonNameType;
extern const bim_type_t t_TVAIDRefElementType;
extern const bim_type_t t_CreditsItemType;
extern const bim_type_t t_CreditsListType;
extern const bim_type_t t_AwardType;
extern const bim_type_t t_AwardsListItemType;
extern const bim_type_t t_AwardsListType;
extern const bim_type_t t_SegmentReferenceType;
extern const bim_type_t t_RelatedMaterialType;
extern const bim_type_t t_TVATimeType;
extern const bim_type_t t_CreationCoordinatesType;
extern const bim_type_t t_HeaderType;
extern const bim_type_t t_TermUseType;
extern const bim_type_t t_PlaceType_AdministrativeUnit;
extern const bim_type_t t_PlaceType_PostalAddress;
extern const bim_type_t t_PlaceType;
extern const bim_type_t t_DepictedCoordinatesType;
extern const bim_type_t t_ReleaseDateType;
extern const bim_type_t t_ReleaseInformationType;
extern const bim_type_t t_PurchaseItemType_Price;
extern const bim_type_t t_PurchaseItemType_Purchase_QuantityRange;
extern const bim_type_t t_PurchaseItemType_Purchase;
extern const bim_type_t t_PurchaseItemType;
extern const bim_type_t t_PurchaseListType;
extern const bim_type_t t_BasicContentDescriptionType;
extern const bim_type_t t_UniqueIDType;
extern const bim_type_t t_BitRateType;
extern const bim_type_t t_AudioLanguageType;
extern const bim_type_t t_AudioAttributesType;
extern const bim_type_t t_AspectRatioType;
extern const bim_type_t t_ColorType;
extern const bim_type_t t_VideoAttributesType;
extern const bim_type_t t_AVAttributesType;
extern const bim_type_t t_CRIDRefType;
extern const bim_type_t t_BaseMemberOfType;
extern const bim_type_t t_BaseDerivationReasonType;
extern const bim_type_t t_DerivedFromType;
extern const bim_type_t t_EpisodeOfType;
extern const bim_type_t t_AggregationOfType;
extern const bim_type_t t_ProgramInformationType;
extern const bim_type_t t_InstanceDescriptionType;
extern const bim_type_t t_FlagType;
extern const bim_type_t t_ScheduleEventType;
extern const bim_type_t t_ScheduleType;
extern const bim_type_t t_ServiceInformationNameType;
extern const bim_type_t t_ValidPeriodType;
extern const bim_type_t t_ServiceRefType;
extern const bim_type_t t_ServiceInformationType;
extern const bim_type_t t_xs_LOCATOR;
extern const bim_type_t t_xs_UNSUPPORTED;
extern const bim_type_t t_xs_UVLU8;
extern const bim_type_t t_xs_STRING;
extern const bim_type_t t_xs_DURATION;
extern const bim_type_t t_xs_DATETIME;

#endif
