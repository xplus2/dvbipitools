/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* generated from TS 102 034 SD&S schemas, ISO 23001-1 rules, zlib strings.
   2014-1: V2.1.1 sdns_v1.6r03, 2012-1: sdns_v1.5r25b, 2008-1: sdns_v1.4r13, 2006: V1.3.1 sdns3r7 */

#include <stdint.h>

#include "lib/bim/sdns.h"

static const char ns_mpeg7[] = "urn:tva:mpeg7:2005";
static const char ns_mpeg7_08[] = "urn:tva:mpeg7:2008";
static const char ns_s06[] = "urn:dvb:ipi:sdns:2006";
static const char ns_s08[] = "urn:dvb:metadata:iptv:sdns:2008-1";
static const char ns_s12[] = "urn:dvb:metadata:iptv:sdns:2012-1";
static const char ns_s14[] = "urn:dvb:metadata:iptv:sdns:2014-1";
static const char ns_tva05[] = "urn:tva:metadata:2005";
static const char ns_tva11[] = "urn:tva:metadata:2011";
static const char ns_xml[] = "http://www.w3.org/XML/1998/namespace";
static const char ns_xs[] = "http://www.w3.org/2001/XMLSchema";

static const bim_wildcard_t w_sd_0;
static const bim_type_t t_sd_xs_anyType;
static const bim_simple_t s_sd_0;
static const bim_type_t t_sd_s08_OfferingBase;
static const bim_simple_t s_sd_1;
static const bim_type_t t_sd_xs_anySimpleType;
static const bim_type_t t_sd_xs_string;
static const bim_type_t t_sd_s08_MultilingualType;
static const bim_type_t t_sd_s12_TargetPackageType;
static const bim_type_t t_sd_s12_PayloadListSegmentType;
static const bim_type_t t_sd_s12_PayloadList_PayloadId;
static const bim_type_t t_sd_s12_PayloadList;
static const bim_simple_t s_sd_2;
static const bim_simple_t s_sd_3;
static const bim_simple_t s_sd_4;
static const bim_type_t t_sd_s14_DVBSTPTransportModeType;
static const bim_type_t t_sd_s08_PayloadList_PayloadId_Segment;
static const bim_type_t t_sd_s08_PayloadList_PayloadId;
static const bim_type_t t_sd_s08_PayloadList;
static const bim_type_t t_sd_s14_HTTPTransportModeType;
static const bim_type_t t_sd_s14_TransportModeType;
static const bim_type_t t_sd_xs_anyURI;
static const bim_type_t t_sd_mpeg7_08_TextualBaseType;
static const bim_type_t t_sd_mpeg7_08_TextualType;
static const bim_simple_t s_sd_5;
static const bim_type_t t_sd_tva11_TermNameType;
static const bim_type_t t_sd_tva11_ControlledTermType;
static const bim_type_t t_sd_s08_DomainType;
static const bim_type_t t_sd_s14_CDSDownloadSessionDescriptionLocationType_DVBSTP;
static const bim_type_t t_sd_s14_CDSDownloadSessionDescriptionLocationType;
static const bim_type_t t_sd_s14_BCGOffering_BCG;
static const bim_type_t t_sd_s14_BCGOffering;
static const bim_type_t t_sd_s08_DescriptionLocation;
static const bim_type_t t_sd_s08_DescriptionLocationBCG;
static const bim_simple_t s_sd_6;
static const bim_simple_t s_sd_7;
static const bim_simple_t s_sd_8;
static const bim_type_t t_sd_s14_FECLayerAddressType;
static const bim_type_t t_sd_xs_decimal;
static const bim_type_t t_sd_xs_integer;
static const bim_type_t t_sd_xs_nonNegativeInteger;
static const bim_simple_t s_sd_9;
static const bim_type_t t_sd_xs_unsignedLong;
static const bim_type_t t_sd_xs_unsignedInt;
static const bim_type_t t_sd_s08_RTCPReportingType;
static const bim_type_t t_sd_s08_UnicastRETType;
static const bim_type_t t_sd_s08_MulticastRETType;
static const bim_type_t t_sd_s08_RETInfoType;
static const bim_simple_t s_sd_10;
static const bim_simple_t s_sd_11;
static const bim_type_t t_sd_s14_McastType;
static const bim_type_t t_sd_s08_RTSP;
static const bim_type_t t_sd_s08_RTSPURLType;
static const bim_type_t t_sd_s12_BroadcastSystemType;
static const bim_type_t t_sd_s14_ServiceLocation;
static const bim_type_t t_sd_s08_TextualIdentifier;
static const bim_type_t t_sd_s08_DVBTriplet;
static const bim_type_t t_sd_xs_positiveInteger;
static const bim_simple_t s_sd_12;
static const bim_type_t t_sd_xs_long;
static const bim_simple_t s_sd_13;
static const bim_type_t t_sd_xs_int;
static const bim_simple_t s_sd_14;
static const bim_type_t t_sd_xs_short;
static const bim_simple_t s_sd_15;
static const bim_type_t t_sd_xs_byte;
static const bim_simple_t s_sd_16;
static const bim_type_t t_sd_s08_Genre;
static const bim_type_t t_sd_s08_CountryAvailability;
static const bim_type_t t_sd_s08_ReplacementService;
static const bim_simple_t s_sd_17;
static const bim_type_t t_sd_s08_MosaicDescription_LogicalCell_ElementaryCell;
static const bim_type_t t_sd_s08_MosaicDescription_LogicalCell_AudioLink;
static const bim_type_t t_sd_s08_Hexadecimal16bit;
static const bim_type_t t_sd_s08_MosaicDescription_LogicalCell_PackageId;
static const bim_type_t t_sd_s08_MosaicDescription_LogicalCell;
static const bim_type_t t_sd_s08_MosaicDescription;
static const bim_type_t t_sd_s08_AnnouncementSupport_Announcement;
static const bim_type_t t_sd_s08_AnnouncementSupport;
static const bim_simple_t s_sd_18;
static const bim_type_t t_sd_s08_SI;
static const bim_type_t t_sd_xs_unsignedShort;
static const bim_type_t t_sd_xs_normalizedString;
static const bim_type_t t_sd_xs_token;
static const bim_type_t t_sd_xs_language;
static const bim_simple_t s_sd_19;
static const bim_type_t t_sd_mpeg7_08_ExtendedLanguageType;
static const bim_type_t t_sd_tva11_AudioLanguageType;
static const bim_type_t t_sd_tva11_BitRateType;
static const bim_type_t t_sd_tva11_AudioAttributesType;
static const bim_simple_t s_sd_20;
static const bim_type_t t_sd_tva11_ScanType;
static const bim_type_t t_sd_tva11_RatioType;
static const bim_simple_t s_sd_21;
static const bim_type_t t_sd_tva11_AspectRatioType;
static const bim_simple_t s_sd_22;
static const bim_type_t t_sd_tva11_ColorType;
static const bim_type_t t_sd_tva11_FrameRateType;
static const bim_type_t t_sd_tva11_VideoAttributesType;
static const bim_type_t t_sd_s08_PackageAvailabilityCountryCodeType;
static const bim_type_t t_sd_s08_ServiceAvailabilityType;
static const bim_simple_t s_sd_23;
static const bim_type_t t_sd_s14_Usage;
static const bim_type_t t_sd_s08_Hexadecimal8bit;
static const bim_type_t t_sd_xs_base64Binary;
static const bim_type_t t_sd_s14_URILinkageType;
static const bim_type_t t_sd_s14_ciAncillaryDataType;
static const bim_type_t t_sd_s14_IPService;
static const bim_type_t t_sd_s14_IPServiceList;
static const bim_type_t t_sd_s14_BroadcastOffering;
static const bim_type_t t_sd_s08_CoDOffering_Catalogue;
static const bim_type_t t_sd_s08_CoDOffering;
static const bim_type_t t_sd_tva11_TVAIDType;
static const bim_type_t t_sd_s12_DescriptionLocationBCG;
static const bim_type_t t_sd_s12_PackageTextualIdentifier;
static const bim_type_t t_sd_s12_DVBTriplet;
static const bim_type_t t_sd_s12_PackagedServiceType;
static const bim_type_t t_sd_s12_Package_PackageReference;
static const bim_type_t t_sd_s12_Package;
static const bim_type_t t_sd_s14_Package;
static const bim_type_t t_sd_s14_PackagedServices;
static const bim_type_t t_sd_s14_DSMMType;
static const bim_simple_t s_sd_24;
static const bim_type_t t_sd_s08_RMSFUSMulticastAddressType;
static const bim_type_t t_sd_s08_FUSAnnouncementType;
static const bim_type_t t_sd_s08_FUSType;
static const bim_type_t t_sd_s14_RMSFUSMulticastAddressType;
static const bim_type_t t_sd_s14_FUSAnnouncementType;
static const bim_type_t t_sd_s14_FUSType;
static const bim_type_t t_sd_s08_RMSType;
static const bim_type_t t_sd_s14_RMSFUSDiscoveryType;
static const bim_type_t t_sd_s08_CivicAddress;
static const bim_type_t t_sd_s08_Cell;
static const bim_type_t t_sd_s08_RegionalizationOffering;
static const bim_type_t t_sd_s12_SRMIDType;
static const bim_type_t t_sd_s14_SRMAnnouncementModeSAPType;
static const bim_type_t t_sd_s14_SRMAnnouncementModeType;
static const bim_type_t t_sd_s14_SRMAnnouncementServiceType;
static const bim_type_t t_sd_s12_SRMIDVerMType;
static const bim_type_t t_sd_s14_SRMDownloadServiceFLUTEType;
static const bim_type_t t_sd_s12_SRMIDVerUType;
static const bim_type_t t_sd_s12_SRMDownloadServiceHTTPType;
static const bim_type_t t_sd_s14_SRMDownloadServiceType;
static const bim_type_t t_sd_s14_SRMOffering;
static const bim_type_t t_sd_s14_OfferingListType_Pull;
static const bim_type_t t_sd_s14_OfferingListType;
static const bim_type_t t_sd_s14_ServiceProviderType;
static const bim_type_t t_sd_s14_ServiceProviderListType;
static const bim_type_t t_sd_s12_ReferencedServiceProviderType_Service;
static const bim_type_t t_sd_s12_ReferencedServiceProviderType;
static const bim_type_t t_sd_s12_ReferencedServices;
static const bim_type_t t_sd_ServiceDiscovery;
static const bim_type_t t_sd_s08_PullURL;
static const bim_type_t t_sd_tva05_CRIDType;
static const bim_type_t t_sd_tva05_InstanceMetadataIdType;
static const bim_type_t t_sd_tva11_CRIDType;
static const bim_type_t t_sd_tva11_InstanceMetadataIdType;
static const bim_simple_t s_sd_25;
static const bim_type_t t_sd_xs_unsignedByte;
static const bim_type_t t_sd_s08_Integer6bit;
static const bim_type_t t_sd_s08_OrigNetId;
static const bim_type_t t_sd_s08_ServiceId;
static const bim_type_t t_sd_s08_TSId;
static const bim_type_t t_sd_tva05_BitRateType;
static const bim_simple_t s_sd_26;
static const bim_type_t t_sd_mpeg7_unsigned12;
static const bim_type_t t_sd_mpeg7_unsigned8;
static const bim_type_t t_sd_mpeg7_08_unsigned12;
static const bim_type_t t_sd_mpeg7_08_unsigned8;
static const bim_type_t t_sd_xs_nonPositiveInteger;
static const bim_type_t t_sd_xs_negativeInteger;
static const bim_type_t t_sd_mpeg7_IncrDurationType;
static const bim_type_t t_sd_mpeg7_MediaIncrDurationType;
static const bim_type_t t_sd_mpeg7_MediaRelIncrTimePointType;
static const bim_type_t t_sd_tva05_TVAMediaRelIncrTimePointType;
static const bim_type_t t_sd_mpeg7_RelIncrTimePointType;
static const bim_simple_t s_sd_27;
static const bim_type_t t_sd_mpeg7_preferenceValueType;
static const bim_type_t t_sd_mpeg7_08_IncrDurationType;
static const bim_type_t t_sd_mpeg7_08_MediaIncrDurationType;
static const bim_type_t t_sd_mpeg7_08_MediaRelIncrTimePointType;
static const bim_type_t t_sd_tva11_TVAMediaRelIncrTimePointType;
static const bim_type_t t_sd_mpeg7_08_RelIncrTimePointType;
static const bim_type_t t_sd_mpeg7_08_preferenceValueType;
static const bim_type_t t_sd_xs_NMTOKEN;
static const bim_simple_t s_sd_28;
static const bim_type_t t_sd_mpeg7_userChoiceType;
static const bim_type_t t_sd_mpeg7_08_userChoiceType;
static const bim_type_t t_sd_xs_Name;
static const bim_type_t t_sd_xs_NCName;
static const bim_type_t t_sd_xs_ENTITY;
static const bim_type_t t_sd_xs_ID;
static const bim_type_t t_sd_xs_IDREF;
static const bim_type_t t_sd_tva05_CaptionLanguageType;
static const bim_type_t t_sd_tva05_SignLanguageType;
static const bim_type_t t_sd_tva11_CaptionLanguageType;
static const bim_type_t t_sd_tva11_SignLanguageType;
static const bim_type_t t_sd_mpeg7_ExtendedLanguageType;
static const bim_type_t t_sd_tva05_AudioLanguageType;
static const bim_type_t t_sd_mpeg7_xPathType;
static const bim_type_t t_sd_mpeg7_xPathRefType;
static const bim_type_t t_sd_mpeg7_xPathSelectorType;
static const bim_type_t t_sd_mpeg7_xPathAbsoluteSelectorType;
static const bim_type_t t_sd_mpeg7_08_xPathType;
static const bim_type_t t_sd_mpeg7_08_xPathRefType;
static const bim_type_t t_sd_mpeg7_08_xPathSelectorType;
static const bim_type_t t_sd_mpeg7_08_xPathAbsoluteSelectorType;
static const bim_type_t t_sd_s08_Hexadecimal32bit;
static const bim_type_t t_sd_s08_Hexadecimal3bit;
static const bim_type_t t_sd_s08_Hexadecimal4bit;
static const bim_type_t t_sd_s08_ServiceType;
static const bim_type_t t_sd_s08_IPType;
static const bim_type_t t_sd_s08_ISO_3166_List;
static const bim_type_t t_sd_s08_ISO639_2;
static const bim_type_t t_sd_s08_PrimarySISource;
static const bim_type_t t_sd_s08_Service;
static const bim_type_t t_sd_s08_StreamingType;
static const bim_type_t t_sd_s08_TransportProtocolType;
static const bim_type_t t_sd_s08_Version;
static const bim_type_t t_sd_s12_CPSystemIDType;
static const bim_type_t t_sd_s12_CPSystemSRMIDType;
static const bim_simple_t s_sd_29;
static const bim_type_t t_sd_s12_EnhancementServiceType;
static const bim_simple_t s_sd_30;
static const bim_type_t t_sd_s12_Usage;
static const bim_type_t t_sd_s14_IPv4Type;
static const bim_type_t t_sd_s14_IPv6Type;
static const bim_type_t t_sd_tva05_ColorTypeType;
static const bim_type_t t_sd_tva05_FrameRateType;
static const bim_type_t t_sd_tva05_RatioType;
static const bim_type_t t_sd_tva05_AspectRatioType;
static const bim_type_t t_sd_tva05_ScanType;
static const bim_simple_t s_sd_31;
static const bim_type_t t_sd_tva05_SynopsisLengthType;
static const bim_type_t t_sd_tva05_TVAIDRefType;
static const bim_type_t t_sd_tva05_TVAIDType;
static const bim_type_t t_sd_tva05_currencyCodeType;
static const bim_simple_t s_sd_32;
static const bim_type_t t_sd_tva05_segmentTypeType;
static const bim_type_t t_sd_tva05_serviceInformationNameLengthType;
static const bim_type_t t_sd_tva11_ColorTypeType;
static const bim_simple_t s_sd_33;
static const bim_type_t t_sd_tva11_DeliveryModeType;
static const bim_type_t t_sd_tva11_ExplanationLengthType;
static const bim_type_t t_sd_tva11_SynopsisLengthType;
static const bim_type_t t_sd_tva11_TVAIDRefType;
static const bim_type_t t_sd_tva11_currencyCodeType;
static const bim_type_t t_sd_tva11_segmentTypeType;
static const bim_type_t t_sd_tva11_serviceInformationNameLengthType;
static const bim_type_t t_sd_mpeg7_TextualBaseType;
static const bim_type_t t_sd_mpeg7_NameComponentType;
static const bim_type_t t_sd_mpeg7_TextualType;
static const bim_simple_t s_sd_34;
static const bim_type_t t_sd_tva05_KeywordType;
static const bim_type_t t_sd_tva05_OrganizationNameType;
static const bim_type_t t_sd_tva05_ServiceInformationNameType;
static const bim_type_t t_sd_tva05_SynopsisType;
static const bim_type_t t_sd_tva05_TermNameType;
static const bim_simple_t s_sd_35;
static const bim_simple_t s_sd_36;
static const bim_type_t t_sd_mpeg7_TitleType;
static const bim_type_t t_sd_tva05_ShortTitleType;
static const bim_simple_t s_sd_37;
static const bim_type_t t_sd_mpeg7_UniqueIDType;
static const bim_type_t t_sd_mpeg7_basicDurationType;
static const bim_type_t t_sd_mpeg7_durationType;
static const bim_type_t t_sd_mpeg7_mediaDurationType;
static const bim_type_t t_sd_mpeg7_mediaTimeOffsetType;
static const bim_type_t t_sd_mpeg7_MediaRelTimePointType;
static const bim_type_t t_sd_mpeg7_timeOffsetType;
static const bim_type_t t_sd_mpeg7_RelTimePointType;
static const bim_type_t t_sd_mpeg7_basicTimePointType;
static const bim_type_t t_sd_mpeg7_mediaTimePointType;
static const bim_type_t t_sd_mpeg7_timePointType;
static const bim_type_t t_sd_mpeg7_characterSetCode;
static const bim_type_t t_sd_mpeg7_countryCode;
static const bim_type_t t_sd_mpeg7_mimeType;
static const bim_type_t t_sd_mpeg7_regionCode;
static const bim_type_t t_sd_mpeg7_08_NameComponentType;
static const bim_type_t t_sd_tva11_ExplanationType;
static const bim_type_t t_sd_tva11_KeywordType;
static const bim_type_t t_sd_tva11_OrganizationNameType;
static const bim_type_t t_sd_tva11_ServiceInformationNameType;
static const bim_type_t t_sd_tva11_SynopsisType;
static const bim_type_t t_sd_mpeg7_08_TitleType;
static const bim_type_t t_sd_tva11_ShortTitleType;
static const bim_type_t t_sd_mpeg7_08_UniqueIDType;
static const bim_type_t t_sd_mpeg7_08_basicDurationType;
static const bim_type_t t_sd_mpeg7_08_durationType;
static const bim_type_t t_sd_mpeg7_08_mediaDurationType;
static const bim_type_t t_sd_mpeg7_08_mediaTimeOffsetType;
static const bim_type_t t_sd_mpeg7_08_MediaRelTimePointType;
static const bim_type_t t_sd_mpeg7_08_timeOffsetType;
static const bim_type_t t_sd_mpeg7_08_RelTimePointType;
static const bim_type_t t_sd_mpeg7_08_basicTimePointType;
static const bim_type_t t_sd_mpeg7_08_mediaTimePointType;
static const bim_type_t t_sd_mpeg7_08_timePointType;
static const bim_type_t t_sd_mpeg7_08_characterSetCode;
static const bim_type_t t_sd_mpeg7_08_countryCode;
static const bim_type_t t_sd_mpeg7_08_mimeType;
static const bim_type_t t_sd_mpeg7_08_regionCode;
static const bim_type_t t_sd_tva11_GenreType;
static const bim_type_t t_sd_s08_DVBSTPTransportModeType;
static const bim_type_t t_sd_s08_HTTPTransportModeType;
static const bim_type_t t_sd_s08_TransportModeType;
static const bim_type_t t_sd_tva05_ControlledTermType;
static const bim_type_t t_sd_s08_CDSDownloadSessionDescriptionLocationType_DVBSTP;
static const bim_type_t t_sd_s08_CDSDownloadSessionDescriptionLocationType;
static const bim_type_t t_sd_s08_BCGOffering_BCG;
static const bim_type_t t_sd_s08_BCGOffering;
static const bim_type_t t_sd_s08_FECLayerAddressType;
static const bim_type_t t_sd_s08_McastType;
static const bim_type_t t_sd_s12_ServiceLocation;
static const bim_type_t t_sd_s12_MosaicDescription_LogicalCell;
static const bim_type_t t_sd_s12_MosaicDescription;
static const bim_type_t t_sd_s12_AnnouncementSupport_Announcement;
static const bim_type_t t_sd_s12_AnnouncementSupport;
static const bim_type_t t_sd_s12_SI;
static const bim_type_t t_sd_s12_IPService;
static const bim_type_t t_sd_s12_IPServiceList;
static const bim_type_t t_sd_s12_BroadcastOffering;
static const bim_type_t t_sd_s12_PackagedServices;
static const bim_type_t t_sd_s08_RMSFUSDiscoveryType;
static const bim_type_t t_sd_s12_SRMAnnouncementModeSAPType;
static const bim_type_t t_sd_s12_SRMAnnouncementModeType;
static const bim_type_t t_sd_s12_SRMAnnouncementServiceType;
static const bim_type_t t_sd_s12_SRMDownloadServiceFLUTEType;
static const bim_type_t t_sd_s12_SRMDownloadServiceType;
static const bim_type_t t_sd_s12_SRMOffering;
static const bim_type_t t_sd_s08_OfferingListType_Pull;
static const bim_type_t t_sd_s08_OfferingListType;
static const bim_type_t t_sd_s08_ServiceProviderType;
static const bim_type_t t_sd_s08_ServiceProviderListType;
static const bim_type_t t_sd_ServiceDiscovery_2;
static const bim_type_t t_sd_tva05_GenreType;
static const bim_type_t t_sd_s08_ServiceLocation;
static const bim_type_t t_sd_tva05_AudioAttributesType;
static const bim_type_t t_sd_tva05_ColorType;
static const bim_type_t t_sd_tva05_VideoAttributesType;
static const bim_type_t t_sd_s08_IPService;
static const bim_type_t t_sd_s08_IPServiceList;
static const bim_type_t t_sd_s08_BroadcastOffering;
static const bim_type_t t_sd_s08_PackagedServiceType;
static const bim_type_t t_sd_s08_Package;
static const bim_type_t t_sd_s08_PackagedServices;
static const bim_type_t t_sd_s08_ReferencedServices_ReferencedServiceProvider;
static const bim_type_t t_sd_s08_ReferencedServices;
static const bim_type_t t_sd_ServiceDiscovery_3;
static const bim_type_t t_sd_s06_OfferingBase;
static const bim_type_t t_sd_s06_MultilingualType;
static const bim_type_t t_sd_s06_PayloadList_PayloadId_Segment;
static const bim_type_t t_sd_s06_PayloadList_PayloadId;
static const bim_type_t t_sd_s06_PayloadList;
static const bim_type_t t_sd_s06_DVBSTPTransportModeType;
static const bim_type_t t_sd_s06_HTTPTransportModeType;
static const bim_type_t t_sd_s06_TransportModeType;
static const bim_type_t t_sd_s06_DomainType;
static const bim_type_t t_sd_s06_BCGOffering_BCG;
static const bim_type_t t_sd_s06_BCGOffering;
static const bim_type_t t_sd_s06_DescriptionLocation;
static const bim_type_t t_sd_s06_DescriptionLocationBCG;
static const bim_type_t t_sd_s06_FECLayerAddressType;
static const bim_type_t t_sd_s06_McastType;
static const bim_type_t t_sd_s06_RTSP;
static const bim_type_t t_sd_s06_ServiceLocation;
static const bim_type_t t_sd_s06_TextualIdentifier;
static const bim_type_t t_sd_s06_DVBTriplet;
static const bim_type_t t_sd_s06_Genre;
static const bim_type_t t_sd_s06_CountryAvailability;
static const bim_type_t t_sd_s06_ReplacementService;
static const bim_type_t t_sd_s06_Hexadecimal16bit;
static const bim_type_t t_sd_s06_MosaicDescription_LogicalCell_PackageId;
static const bim_type_t t_sd_s06_MosaicDescription_LogicalCell;
static const bim_type_t t_sd_s06_MosaicDescription;
static const bim_type_t t_sd_s06_AnnouncementSupport_Announcement;
static const bim_type_t t_sd_s06_AnnouncementSupport;
static const bim_type_t t_sd_s06_SI;
static const bim_type_t t_sd_s06_PackageAvailabilityCountryCodeType;
static const bim_type_t t_sd_s06_ServiceAvailabilityType;
static const bim_type_t t_sd_s06_IPService;
static const bim_type_t t_sd_s06_IPServiceList;
static const bim_type_t t_sd_s06_BroadcastOffering;
static const bim_type_t t_sd_s06_CoDOffering_Catalogue;
static const bim_type_t t_sd_s06_CoDOffering;
static const bim_type_t t_sd_s06_PackagedServiceType;
static const bim_type_t t_sd_s06_Package;
static const bim_type_t t_sd_s06_PackagedServices;
static const bim_type_t t_sd_s06_OfferingListType_Pull;
static const bim_type_t t_sd_s06_OfferingListType;
static const bim_type_t t_sd_s06_ServiceProvider_ServiceProvider;
static const bim_type_t t_sd_s06_ServiceProvider;
static const bim_type_t t_sd_s06_ReferencedServices_ReferencedServiceProvider;
static const bim_type_t t_sd_s06_ReferencedServices;
static const bim_type_t t_sd_ServiceDiscovery_4;
static const bim_type_t t_sd_s06_PullURL;
static const bim_type_t t_sd_s06_Integer6bit;
static const bim_type_t t_sd_s06_OrigNetId;
static const bim_type_t t_sd_s06_ServiceId;
static const bim_type_t t_sd_s06_TSId;
static const bim_type_t t_sd_s06_Hexadecimal3bit;
static const bim_type_t t_sd_s06_Hexadecimal4bit;
static const bim_type_t t_sd_s06_Hexadecimal8bit;
static const bim_type_t t_sd_s06_ServiceType;
static const bim_type_t t_sd_s06_IPType;
static const bim_type_t t_sd_s06_ISO_3166_List;
static const bim_type_t t_sd_s06_ISO639_2;
static const bim_type_t t_sd_s06_PrimarySISource;
static const bim_type_t t_sd_s06_Service;
static const bim_type_t t_sd_s06_StreamingType;

static const bim_wildcard_t w_sd_0 = {.kind = BIM_WC_ANY, .process = BIM_PC_LAX};
static const bim_part_t t_sd_xs_anyType_top = {.kind = BIM_PART_ANY, .min = 0, .max = BIM_UNBOUNDED, .wc = &w_sd_0};
static const bim_type_t t_sd_xs_anyType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_xs_anyType_top, .name = "anyType", .ns = ns_xs, .mixed = 1, .anyattr = &w_sd_0};
static const bim_simple_t s_sd_0 = {.codec = BIM_ZSTRING};
static const bim_attr_t t_sd_s08_OfferingBase_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_type_t t_sd_s08_OfferingBase = {.attrs = t_sd_s08_OfferingBase_a, .nattrs = 2, .content = BIM_CONTENT_NONE, .name = "OfferingBase", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_simple_t s_sd_1 = {.codec = BIM_UNSUPPORTED};
static const bim_type_t t_sd_xs_anySimpleType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_1, .name = "anySimpleType", .ns = ns_xs, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_xs_string = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "string", .ns = ns_xs, .base = &t_sd_xs_anySimpleType};
static const bim_attr_t t_sd_s08_MultilingualType_a[] = {
  {.name = "Language", .required = 1, .simple = &s_sd_0}
};
static const bim_type_t t_sd_s08_MultilingualType = {.attrs = t_sd_s08_MultilingualType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "MultilingualType", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_attr_t t_sd_s12_TargetPackageType_a[] = {
  {.name = "PackageIDRef", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_TargetPackageType_top = {.kind = BIM_PART_ELEM, .name = "PackageType", .ns = ns_s12, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s12_TargetPackageType = {.attrs = t_sd_s12_TargetPackageType_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_TargetPackageType_top, .name = "TargetPackageType", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_PayloadListSegmentType_a[] = {
  {.name = "ID", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_PayloadListSegmentType_top = {.kind = BIM_PART_ELEM, .name = "TargetPackage", .ns = ns_s12, .type = &t_sd_s12_TargetPackageType, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s12_PayloadListSegmentType = {.attrs = t_sd_s12_PayloadListSegmentType_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_PayloadListSegmentType_top, .name = "PayloadListSegmentType", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_PayloadList_PayloadId_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_PayloadList_PayloadId_top = {.kind = BIM_PART_ELEM, .name = "Segment", .ns = ns_s12, .type = &t_sd_s12_PayloadListSegmentType, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s12_PayloadList_PayloadId = {.attrs = t_sd_s12_PayloadList_PayloadId_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_PayloadList_PayloadId_top, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s12_PayloadList_top = {.kind = BIM_PART_ELEM, .name = "PayloadId", .ns = ns_s12, .type = &t_sd_s12_PayloadList_PayloadId, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s12_PayloadList = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s12_PayloadList_top, .name = "PayloadList", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_simple_t *const s_sd_2_m[] = {&s_sd_0, &s_sd_0};
static const bim_simple_t s_sd_2 = {.codec = BIM_UNION, .members = s_sd_2_m, .nmembers = 2};
static const bim_simple_t *const s_sd_3_m[] = {&s_sd_2, &s_sd_0};
static const bim_simple_t s_sd_3 = {.codec = BIM_UNION, .members = s_sd_3_m, .nmembers = 2};
static const bim_simple_t s_sd_4 = {.codec = BIM_RANGE, .range = {INT64_C(0), UINT64_C(65535)}};
static const bim_attr_t t_sd_s14_DVBSTPTransportModeType_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_3},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_3}
};
static const bim_part_t t_sd_s14_DVBSTPTransportModeType_top = {.kind = BIM_PART_ELEM, .name = "PayloadId", .ns = ns_s12, .type = &t_sd_s12_PayloadList_PayloadId, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s14_DVBSTPTransportModeType = {.attrs = t_sd_s14_DVBSTPTransportModeType_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_DVBSTPTransportModeType_top, .name = "DVBSTPTransportModeType", .ns = ns_s14, .base = &t_sd_s12_PayloadList};
static const bim_attr_t t_sd_s08_PayloadList_PayloadId_Segment_a[] = {
  {.name = "ID", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_type_t t_sd_s08_PayloadList_PayloadId_Segment = {.attrs = t_sd_s08_PayloadList_PayloadId_Segment_a, .nattrs = 2, .content = BIM_CONTENT_NONE, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_PayloadList_PayloadId_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_PayloadList_PayloadId_top = {.kind = BIM_PART_ELEM, .name = "Segment", .ns = ns_s08, .type = &t_sd_s08_PayloadList_PayloadId_Segment, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_PayloadList_PayloadId = {.attrs = t_sd_s08_PayloadList_PayloadId_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_PayloadList_PayloadId_top, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s08_PayloadList_top = {.kind = BIM_PART_ELEM, .name = "PayloadId", .ns = ns_s08, .type = &t_sd_s08_PayloadList_PayloadId, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_PayloadList = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s08_PayloadList_top, .name = "PayloadList", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s14_HTTPTransportModeType_a[] = {
  {.name = "Location", .required = 1, .simple = &s_sd_0},
  {.name = "PollingPeriodSecs", .simple = &s_sd_4},
  {.name = "SOAP", .simple = &s_sd_1},
  {.name = "SpreadRequestSecs", .simple = &s_sd_4}
};
static const bim_part_t t_sd_s14_HTTPTransportModeType_top = {.kind = BIM_PART_ELEM, .name = "PayloadId", .ns = ns_s08, .type = &t_sd_s08_PayloadList_PayloadId, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s14_HTTPTransportModeType = {.attrs = t_sd_s14_HTTPTransportModeType_a, .nattrs = 4, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_HTTPTransportModeType_top, .name = "HTTPTransportModeType", .ns = ns_s14, .base = &t_sd_s08_PayloadList};
static const bim_part_t t_sd_s14_TransportModeType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBSTP", .ns = ns_s14, .type = &t_sd_s14_DVBSTPTransportModeType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "HTTP", .ns = ns_s14, .type = &t_sd_s14_HTTPTransportModeType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s14_TransportModeType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = BIM_UNBOUNDED, .kids = t_sd_s14_TransportModeType_p, .nkids = 2};
static const bim_type_t t_sd_s14_TransportModeType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s14_TransportModeType_top, .name = "TransportModeType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_xs_anyURI = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "anyURI", .ns = ns_xs, .base = &t_sd_xs_anySimpleType};
static const bim_attr_t t_sd_mpeg7_08_TextualBaseType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_08_TextualBaseType = {.attrs = t_sd_mpeg7_08_TextualBaseType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "TextualBaseType", .ns = ns_mpeg7_08, .base = &t_sd_xs_string, .abstract = 1};
static const bim_attr_t t_sd_mpeg7_08_TextualType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_08_TextualType = {.attrs = t_sd_mpeg7_08_TextualType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "TextualType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_TextualBaseType};
static const bim_simple_t s_sd_5 = {.codec = BIM_BOOLEAN};
static const bim_attr_t t_sd_tva11_TermNameType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "preferred", .simple = &s_sd_5}
};
static const bim_type_t t_sd_tva11_TermNameType = {.attrs = t_sd_tva11_TermNameType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "TermNameType", .ns = ns_tva11, .base = &t_sd_mpeg7_08_TextualType};
static const bim_attr_t t_sd_tva11_ControlledTermType_a[] = {
  {.name = "href", .required = 1, .simple = &s_sd_2}
};
static const bim_part_t t_sd_tva11_ControlledTermType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_tva11, .type = &t_sd_tva11_TermNameType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Definition", .ns = ns_tva11, .type = &t_sd_mpeg7_08_TextualType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_tva11_ControlledTermType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_tva11_ControlledTermType_p, .nkids = 2};
static const bim_type_t t_sd_tva11_ControlledTermType = {.attrs = t_sd_tva11_ControlledTermType_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_tva11_ControlledTermType_top, .name = "ControlledTermType", .ns = ns_tva11, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_s08_DomainType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "DomainType", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_attr_t t_sd_s14_CDSDownloadSessionDescriptionLocationType_DVBSTP_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_3},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_3}
};
static const bim_type_t t_sd_s14_CDSDownloadSessionDescriptionLocationType_DVBSTP = {.attrs = t_sd_s14_CDSDownloadSessionDescriptionLocationType_DVBSTP_a, .nattrs = 3, .content = BIM_CONTENT_NONE, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s14_CDSDownloadSessionDescriptionLocationType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBSTP", .ns = ns_s14, .type = &t_sd_s14_CDSDownloadSessionDescriptionLocationType_DVBSTP, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "SAP", .ns = ns_s14, .type = &t_sd_s14_CDSDownloadSessionDescriptionLocationType_DVBSTP, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s14_CDSDownloadSessionDescriptionLocationType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = BIM_UNBOUNDED, .kids = t_sd_s14_CDSDownloadSessionDescriptionLocationType_p, .nkids = 2};
static const bim_type_t t_sd_s14_CDSDownloadSessionDescriptionLocationType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s14_CDSDownloadSessionDescriptionLocationType_top, .name = "CDSDownloadSessionDescriptionLocationType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s14_BCGOffering_BCG_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s14_BCGOffering_BCG_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_s14, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s14, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "TransportMode", .ns = ns_s14, .type = &t_sd_s14_TransportModeType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Logo", .ns = ns_s14, .type = &t_sd_xs_anyURI, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Type", .ns = ns_s14, .type = &t_sd_tva11_ControlledTermType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TargetProvider", .ns = ns_s14, .type = &t_sd_s08_DomainType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "BCGProviderName", .ns = ns_s14, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CDSDownloadSessionDescriptionLocation", .ns = ns_s14, .type = &t_sd_s14_CDSDownloadSessionDescriptionLocationType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s14_BCGOffering_BCG_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_BCGOffering_BCG_p, .nkids = 8};
static const bim_type_t t_sd_s14_BCGOffering_BCG = {.attrs = t_sd_s14_BCGOffering_BCG_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_BCGOffering_BCG_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s14_BCGOffering_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s14_BCGOffering_top = {.kind = BIM_PART_ELEM, .name = "BCG", .ns = ns_s14, .type = &t_sd_s14_BCGOffering_BCG, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s14_BCGOffering = {.attrs = t_sd_s14_BCGOffering_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_BCGOffering_top, .name = "BCGOffering", .ns = ns_s14, .base = &t_sd_s08_OfferingBase};
static const bim_type_t t_sd_s08_DescriptionLocation = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "DescriptionLocation", .ns = ns_s08, .base = &t_sd_xs_anyURI};
static const bim_attr_t t_sd_s08_DescriptionLocationBCG_a[] = {
  {.name = "preferred", .simple = &s_sd_5}
};
static const bim_type_t t_sd_s08_DescriptionLocationBCG = {.attrs = t_sd_s08_DescriptionLocationBCG_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "DescriptionLocationBCG", .ns = ns_s08, .base = &t_sd_s08_DescriptionLocation};
static const bim_simple_t s_sd_6 = {.codec = BIM_SVLU5};
static const bim_simple_t s_sd_7 = {.codec = BIM_RANGE, .range = {INT64_C(0), UINT64_C(4294967295)}};
static const char *const s_sd_8_v[] = {"RTP-AVP", "UDP-FEC"};
static const bim_simple_t s_sd_8 = {.codec = BIM_ENUM, .enums = s_sd_8_v, .nenums = 2};
static const bim_attr_t t_sd_s14_FECLayerAddressType_a[] = {
  {.name = "Address", .simple = &s_sd_3},
  {.name = "MaxBitrate", .simple = &s_sd_6},
  {.name = "PayloadTypeNumber", .simple = &s_sd_7},
  {.name = "Port", .simple = &s_sd_4},
  {.name = "RTSPControlURL", .simple = &s_sd_0},
  {.name = "Source", .simple = &s_sd_3},
  {.name = "TransportProtocol", .simple = &s_sd_8}
};
static const bim_type_t t_sd_s14_FECLayerAddressType = {.attrs = t_sd_s14_FECLayerAddressType_a, .nattrs = 7, .content = BIM_CONTENT_NONE, .name = "FECLayerAddressType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_xs_decimal = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "decimal", .ns = ns_xs, .base = &t_sd_xs_anySimpleType};
static const bim_type_t t_sd_xs_integer = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "integer", .ns = ns_xs, .base = &t_sd_xs_decimal};
static const bim_type_t t_sd_xs_nonNegativeInteger = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "nonNegativeInteger", .ns = ns_xs, .base = &t_sd_xs_integer};
static const bim_simple_t s_sd_9 = {.codec = BIM_RANGE, .range = {INT64_C(0), UINT64_MAX}};
static const bim_type_t t_sd_xs_unsignedLong = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_9, .name = "unsignedLong", .ns = ns_xs, .base = &t_sd_xs_nonNegativeInteger};
static const bim_type_t t_sd_xs_unsignedInt = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_7, .name = "unsignedInt", .ns = ns_xs, .base = &t_sd_xs_unsignedLong};
static const bim_attr_t t_sd_s08_RTCPReportingType_a[] = {
  {.name = "DestinationAddress", .required = 1, .simple = &s_sd_0},
  {.name = "DestinationPort", .simple = &s_sd_4},
  {.name = "dvb-disable-rtcp-rr", .simple = &s_sd_5},
  {.name = "dvb-enable-byte", .simple = &s_sd_5},
  {.name = "dvb-rsi-mc-ret", .simple = &s_sd_5},
  {.name = "dvb-ssrc-bitmask", .simple = &s_sd_0},
  {.name = "dvb-ssrc-upstream-client", .simple = &s_sd_6},
  {.name = "dvb-t-ret", .simple = &s_sd_6},
  {.name = "dvb-t-wait-max", .simple = &s_sd_7},
  {.name = "dvb-t-wait-min", .simple = &s_sd_7},
  {.name = "rtcp-bandwidth", .simple = &s_sd_6},
  {.name = "rtcp-rsize", .simple = &s_sd_6},
  {.name = "trr-int", .simple = &s_sd_6}
};
static const bim_type_t t_sd_s08_RTCPReportingType = {.attrs = t_sd_s08_RTCPReportingType_a, .nattrs = 13, .content = BIM_CONTENT_NONE, .name = "RTCPReportingType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_UnicastRETType_a[] = {
  {.name = "DestinationPort", .simple = &s_sd_7},
  {.name = "DestinationPort-ForRTCPReporting", .simple = &s_sd_7},
  {.name = "RTPPayloadTypeNumber", .simple = &s_sd_7},
  {.name = "RTSPControlURL", .simple = &s_sd_0},
  {.name = "SourcePort", .simple = &s_sd_7},
  {.name = "dvb-original-copy-ret", .simple = &s_sd_5},
  {.name = "rtcp-mux", .simple = &s_sd_5},
  {.name = "rtx-time", .required = 1, .simple = &s_sd_7},
  {.name = "ssrc", .simple = &s_sd_7},
  {.name = "trr-int", .simple = &s_sd_7}
};
static const bim_type_t t_sd_s08_UnicastRETType = {.attrs = t_sd_s08_UnicastRETType_a, .nattrs = 10, .content = BIM_CONTENT_NONE, .name = "UnicastRETType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_MulticastRETType_a[] = {
  {.name = "DestinationPort", .simple = &s_sd_7},
  {.name = "GroupAddress", .required = 1, .simple = &s_sd_0},
  {.name = "RTPPayloadTypeNumber", .simple = &s_sd_7},
  {.name = "SourceAddress", .simple = &s_sd_0},
  {.name = "dvb-original-copy-ret", .simple = &s_sd_5},
  {.name = "rtcp-mux", .simple = &s_sd_5},
  {.name = "rtx-time", .required = 1, .simple = &s_sd_7},
  {.name = "ssrc", .simple = &s_sd_7}
};
static const bim_type_t t_sd_s08_MulticastRETType = {.attrs = t_sd_s08_MulticastRETType_a, .nattrs = 8, .content = BIM_CONTENT_NONE, .name = "MulticastRETType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s08_RETInfoType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "RTCPReporting", .ns = ns_s08, .type = &t_sd_s08_RTCPReportingType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "UnicastRET", .ns = ns_s08, .type = &t_sd_s08_UnicastRETType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "MulticastRET", .ns = ns_s08, .type = &t_sd_s08_MulticastRETType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s08_RETInfoType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_RETInfoType_p, .nkids = 3};
static const bim_type_t t_sd_s08_RETInfoType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s08_RETInfoType_top, .name = "RETInfoType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_simple_t s_sd_10 = {.codec = BIM_BASE64};
static const char *const s_sd_11_v[] = {"rtp", "udp"};
static const bim_simple_t s_sd_11 = {.codec = BIM_ENUM, .enums = s_sd_11_v, .nenums = 2};
static const bim_attr_t t_sd_s14_McastType_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_3},
  {.name = "FECMaxBlockSize", .simple = &s_sd_4},
  {.name = "FECMaxBlockTime", .simple = &s_sd_4},
  {.name = "FECOTI", .simple = &s_sd_10},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_3},
  {.name = "Streaming", .simple = &s_sd_11}
};
static const bim_part_t t_sd_s14_McastType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "FECBaseLayer", .ns = ns_s14, .type = &t_sd_s14_FECLayerAddressType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "FECEnhancementLayer", .ns = ns_s14, .type = &t_sd_s14_FECLayerAddressType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CNAME", .ns = ns_s14, .type = &t_sd_xs_string, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "ssrc", .ns = ns_s14, .type = &t_sd_xs_unsignedInt, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "RTPRetransmission", .ns = ns_s14, .type = &t_sd_s08_RETInfoType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "ServerBasedLMBEnhancementService", .ns = ns_s14, .type = &t_sd_s08_RETInfoType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s14_McastType_top = {.kind = BIM_PART_SEQ, .min = 0, .max = 1, .kids = t_sd_s14_McastType_p, .nkids = 6};
static const bim_type_t t_sd_s14_McastType = {.attrs = t_sd_s14_McastType_a, .nattrs = 7, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_McastType_top, .name = "McastType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_s08_RTSP = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "RTSP", .ns = ns_s08, .base = &t_sd_xs_anyURI};
static const bim_attr_t t_sd_s08_RTSPURLType_a[] = {
  {.name = "RTSPControlURL", .simple = &s_sd_0}
};
static const bim_type_t t_sd_s08_RTSPURLType = {.attrs = t_sd_s08_RTSPURLType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "RTSPURLType", .ns = ns_s08, .base = &t_sd_s08_RTSP};
static const bim_type_t t_sd_s12_BroadcastSystemType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "BroadcastSystemType", .ns = ns_s12, .base = &t_sd_xs_string};
static const bim_part_t t_sd_s14_ServiceLocation_p_0[] = {
  {.kind = BIM_PART_ELEM, .name = "IPMulticastAddress", .ns = ns_s14, .type = &t_sd_s14_McastType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "RTSPURL", .ns = ns_s14, .type = &t_sd_s08_RTSPURLType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s14_ServiceLocation_p[] = {
  {.kind = BIM_PART_CHOICE, .min = 1, .max = BIM_UNBOUNDED, .kids = t_sd_s14_ServiceLocation_p_0, .nkids = 2},
  {.kind = BIM_PART_ELEM, .name = "BroadcastSystem", .ns = ns_s14, .type = &t_sd_s12_BroadcastSystemType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s14_ServiceLocation_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = 1, .kids = t_sd_s14_ServiceLocation_p, .nkids = 2};
static const bim_type_t t_sd_s14_ServiceLocation = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s14_ServiceLocation_top, .name = "ServiceLocation", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_TextualIdentifier_a[] = {
  {.name = "DomainName", .simple = &s_sd_0},
  {.name = "ServiceName", .required = 1, .simple = &s_sd_0}
};
static const bim_type_t t_sd_s08_TextualIdentifier = {.attrs = t_sd_s08_TextualIdentifier_a, .nattrs = 2, .content = BIM_CONTENT_NONE, .name = "TextualIdentifier", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_DVBTriplet_a[] = {
  {.name = "OrigNetId", .required = 1, .simple = &s_sd_4},
  {.name = "ServiceId", .required = 1, .simple = &s_sd_4},
  {.name = "TSId", .required = 1, .simple = &s_sd_4}
};
static const bim_type_t t_sd_s08_DVBTriplet = {.attrs = t_sd_s08_DVBTriplet_a, .nattrs = 3, .content = BIM_CONTENT_NONE, .name = "DVBTriplet", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_xs_positiveInteger = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "positiveInteger", .ns = ns_xs, .base = &t_sd_xs_nonNegativeInteger};
static const bim_simple_t s_sd_12 = {.codec = BIM_RANGE, .range = {INT64_MIN, UINT64_MAX}};
static const bim_type_t t_sd_xs_long = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_12, .name = "long", .ns = ns_xs, .base = &t_sd_xs_integer};
static const bim_simple_t s_sd_13 = {.codec = BIM_RANGE, .range = {INT64_C(-2147483648), UINT64_C(4294967295)}};
static const bim_type_t t_sd_xs_int = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_13, .name = "int", .ns = ns_xs, .base = &t_sd_xs_long};
static const bim_simple_t s_sd_14 = {.codec = BIM_RANGE, .range = {INT64_C(-32768), UINT64_C(65535)}};
static const bim_type_t t_sd_xs_short = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_14, .name = "short", .ns = ns_xs, .base = &t_sd_xs_int};
static const bim_simple_t s_sd_15 = {.codec = BIM_RANGE, .range = {INT64_C(-128), UINT64_C(255)}};
static const bim_type_t t_sd_xs_byte = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_15, .name = "byte", .ns = ns_xs, .base = &t_sd_xs_short};
static const bim_simple_t s_sd_16 = {.codec = BIM_RANGE, .range = {INT64_C(0), UINT64_C(15)}};
static const bim_type_t t_sd_s08_Genre = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_16, .name = "Genre", .ns = ns_s08, .base = &t_sd_xs_byte};
static const bim_attr_t t_sd_s08_CountryAvailability_a[] = {
  {.name = "Available", .simple = &s_sd_5},
  {.name = "Countries", .required = 1, .simple = &s_sd_0}
};
static const bim_type_t t_sd_s08_CountryAvailability = {.attrs = t_sd_s08_CountryAvailability_a, .nattrs = 2, .content = BIM_CONTENT_NONE, .name = "CountryAvailability", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_ReplacementService_a[] = {
  {.name = "ReplacementType", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_ReplacementService_p[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s08, .type = &t_sd_s08_DVBTriplet, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TextualIdentifier", .ns = ns_s08, .type = &t_sd_s08_TextualIdentifier, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s08_ReplacementService_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = 1, .kids = t_sd_s08_ReplacementService_p, .nkids = 2};
static const bim_type_t t_sd_s08_ReplacementService = {.attrs = t_sd_s08_ReplacementService_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_ReplacementService_top, .name = "ReplacementService", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_simple_t s_sd_17 = {.codec = BIM_RANGE, .range = {INT64_C(0), UINT64_C(63)}};
static const bim_attr_t t_sd_s08_MosaicDescription_LogicalCell_ElementaryCell_a[] = {
  {.name = "CellId", .required = 1, .simple = &s_sd_17}
};
static const bim_type_t t_sd_s08_MosaicDescription_LogicalCell_ElementaryCell = {.attrs = t_sd_s08_MosaicDescription_LogicalCell_ElementaryCell_a, .nattrs = 1, .content = BIM_CONTENT_NONE, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_MosaicDescription_LogicalCell_AudioLink_a[] = {
  {.name = "ComponentTag", .required = 1, .simple = &s_sd_0},
  {.name = "Language", .simple = &s_sd_0}
};
static const bim_type_t t_sd_s08_MosaicDescription_LogicalCell_AudioLink = {.attrs = t_sd_s08_MosaicDescription_LogicalCell_AudioLink_a, .nattrs = 2, .content = BIM_CONTENT_NONE, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_s08_Hexadecimal16bit = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Hexadecimal16bit", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_attr_t t_sd_s08_MosaicDescription_LogicalCell_PackageId_a[] = {
  {.name = "Domain", .simple = &s_sd_0}
};
static const bim_type_t t_sd_s08_MosaicDescription_LogicalCell_PackageId = {.attrs = t_sd_s08_MosaicDescription_LogicalCell_PackageId_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .base = &t_sd_s08_Hexadecimal16bit};
static const bim_attr_t t_sd_s08_MosaicDescription_LogicalCell_a[] = {
  {.name = "CellId", .required = 1, .simple = &s_sd_17},
  {.name = "EventId", .simple = &s_sd_0},
  {.name = "LinkageInfo", .required = 1, .simple = &s_sd_0},
  {.name = "PresentationInfo", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_MosaicDescription_LogicalCell_p_2[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s08, .type = &t_sd_s08_DVBTriplet, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "PackageId", .ns = ns_s08, .type = &t_sd_s08_MosaicDescription_LogicalCell_PackageId, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TextualId", .ns = ns_s08, .type = &t_sd_s08_TextualIdentifier, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s08_MosaicDescription_LogicalCell_p[] = {
  {.kind = BIM_PART_ELEM, .name = "ElementaryCell", .ns = ns_s08, .type = &t_sd_s08_MosaicDescription_LogicalCell_ElementaryCell, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "AudioLink", .ns = ns_s08, .type = &t_sd_s08_MosaicDescription_LogicalCell_AudioLink, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_CHOICE, .min = 0, .max = 1, .kids = t_sd_s08_MosaicDescription_LogicalCell_p_2, .nkids = 3}
};
static const bim_part_t t_sd_s08_MosaicDescription_LogicalCell_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_MosaicDescription_LogicalCell_p, .nkids = 3};
static const bim_type_t t_sd_s08_MosaicDescription_LogicalCell = {.attrs = t_sd_s08_MosaicDescription_LogicalCell_a, .nattrs = 4, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_MosaicDescription_LogicalCell_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_MosaicDescription_a[] = {
  {.name = "EntryPoint", .simple = &s_sd_5},
  {.name = "HorizontalCellsNumber", .required = 1, .simple = &s_sd_0},
  {.name = "VerticalCellsNumber", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_MosaicDescription_top = {.kind = BIM_PART_ELEM, .name = "LogicalCell", .ns = ns_s08, .type = &t_sd_s08_MosaicDescription_LogicalCell, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_MosaicDescription = {.attrs = t_sd_s08_MosaicDescription_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_MosaicDescription_top, .name = "MosaicDescription", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_AnnouncementSupport_Announcement_a[] = {
  {.name = "ComponentTag", .simple = &s_sd_0},
  {.name = "ReferenceType", .required = 1, .simple = &s_sd_0},
  {.name = "Type", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_AnnouncementSupport_Announcement_p[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s08, .type = &t_sd_s08_DVBTriplet, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TextualIdentifier", .ns = ns_s08, .type = &t_sd_s08_TextualIdentifier, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s08_AnnouncementSupport_Announcement_top = {.kind = BIM_PART_CHOICE, .min = 0, .max = 1, .kids = t_sd_s08_AnnouncementSupport_Announcement_p, .nkids = 2};
static const bim_type_t t_sd_s08_AnnouncementSupport_Announcement = {.attrs = t_sd_s08_AnnouncementSupport_Announcement_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_AnnouncementSupport_Announcement_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_AnnouncementSupport_a[] = {
  {.name = "SupportIndicator", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_AnnouncementSupport_top = {.kind = BIM_PART_ELEM, .name = "Announcement", .ns = ns_s08, .type = &t_sd_s08_AnnouncementSupport_Announcement, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_AnnouncementSupport = {.attrs = t_sd_s08_AnnouncementSupport_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_AnnouncementSupport_top, .name = "AnnouncementSupport", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const char *const s_sd_18_v[] = {"Stream", "XML"};
static const bim_simple_t s_sd_18 = {.codec = BIM_ENUM, .enums = s_sd_18_v, .nenums = 2};
static const bim_attr_t t_sd_s08_SI_a[] = {
  {.name = "PrimarySISource", .simple = &s_sd_18},
  {.name = "ServiceType", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_SI_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServiceDescriptionLocation", .ns = ns_s08, .type = &t_sd_s08_DescriptionLocationBCG, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ContentGenre", .ns = ns_s08, .type = &t_sd_s08_Genre, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CountryAvailability", .ns = ns_s08, .type = &t_sd_s08_CountryAvailability, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ReplacementService", .ns = ns_s08, .type = &t_sd_s08_ReplacementService, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "MosaicDescription", .ns = ns_s08, .type = &t_sd_s08_MosaicDescription, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "AnnouncementSupport", .ns = ns_s08, .type = &t_sd_s08_AnnouncementSupport, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s08_SI_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_SI_p, .nkids = 8};
static const bim_type_t t_sd_s08_SI = {.attrs = t_sd_s08_SI_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_SI_top, .name = "SI", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_xs_unsignedShort = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_4, .name = "unsignedShort", .ns = ns_xs, .base = &t_sd_xs_unsignedInt};
static const bim_type_t t_sd_xs_normalizedString = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "normalizedString", .ns = ns_xs, .base = &t_sd_xs_string};
static const bim_type_t t_sd_xs_token = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "token", .ns = ns_xs, .base = &t_sd_xs_normalizedString};
static const bim_type_t t_sd_xs_language = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "language", .ns = ns_xs, .base = &t_sd_xs_token};
static const char *const s_sd_19_v[] = {"background", "dubbed", "original"};
static const bim_simple_t s_sd_19 = {.codec = BIM_ENUM, .enums = s_sd_19_v, .nenums = 3};
static const bim_attr_t t_sd_mpeg7_08_ExtendedLanguageType_a[] = {
  {.name = "supplemental", .simple = &s_sd_5},
  {.name = "type", .simple = &s_sd_19}
};
static const bim_type_t t_sd_mpeg7_08_ExtendedLanguageType = {.attrs = t_sd_mpeg7_08_ExtendedLanguageType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ExtendedLanguageType", .ns = ns_mpeg7_08, .base = &t_sd_xs_language};
static const bim_attr_t t_sd_tva11_AudioLanguageType_a[] = {
  {.name = "purpose", .simple = &s_sd_2},
  {.name = "supplemental", .simple = &s_sd_5},
  {.name = "type", .simple = &s_sd_19}
};
static const bim_type_t t_sd_tva11_AudioLanguageType = {.attrs = t_sd_tva11_AudioLanguageType_a, .nattrs = 3, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "AudioLanguageType", .ns = ns_tva11, .base = &t_sd_mpeg7_08_ExtendedLanguageType};
static const bim_attr_t t_sd_tva11_BitRateType_a[] = {
  {.name = "average", .simple = &s_sd_9},
  {.name = "maximum", .simple = &s_sd_9},
  {.name = "minimum", .simple = &s_sd_9},
  {.name = "variable", .simple = &s_sd_5}
};
static const bim_type_t t_sd_tva11_BitRateType = {.attrs = t_sd_tva11_BitRateType_a, .nattrs = 4, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "BitRateType", .ns = ns_tva11, .base = &t_sd_xs_nonNegativeInteger};
static const bim_part_t t_sd_tva11_AudioAttributesType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Coding", .ns = ns_tva11, .type = &t_sd_tva11_ControlledTermType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "NumOfChannels", .ns = ns_tva11, .type = &t_sd_xs_unsignedShort, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "MixType", .ns = ns_tva11, .type = &t_sd_tva11_ControlledTermType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "AudioLanguage", .ns = ns_tva11, .type = &t_sd_tva11_AudioLanguageType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "SampleFrequency", .ns = ns_tva11, .type = &t_sd_xs_unsignedInt, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "BitsPerSample", .ns = ns_tva11, .type = &t_sd_xs_unsignedInt, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "BitRate", .ns = ns_tva11, .type = &t_sd_tva11_BitRateType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_tva11_AudioAttributesType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_tva11_AudioAttributesType_p, .nkids = 7};
static const bim_type_t t_sd_tva11_AudioAttributesType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_tva11_AudioAttributesType_top, .name = "AudioAttributesType", .ns = ns_tva11, .base = &t_sd_xs_anyType};
static const char *const s_sd_20_v[] = {"interlaced", "progressive"};
static const bim_simple_t s_sd_20 = {.codec = BIM_ENUM, .enums = s_sd_20_v, .nenums = 2};
static const bim_type_t t_sd_tva11_ScanType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_20, .name = "ScanType", .ns = ns_tva11, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva11_RatioType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "RatioType", .ns = ns_tva11, .base = &t_sd_xs_string};
static const char *const s_sd_21_v[] = {"original", "publication"};
static const bim_simple_t s_sd_21 = {.codec = BIM_ENUM, .enums = s_sd_21_v, .nenums = 2};
static const bim_attr_t t_sd_tva11_AspectRatioType_a[] = {
  {.name = "type", .simple = &s_sd_21}
};
static const bim_type_t t_sd_tva11_AspectRatioType = {.attrs = t_sd_tva11_AspectRatioType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "AspectRatioType", .ns = ns_tva11, .base = &t_sd_tva11_RatioType};
static const char *const s_sd_22_v[] = {"blackAndWhite", "blackAndWhiteAndColor", "color", "colorized"};
static const bim_simple_t s_sd_22 = {.codec = BIM_ENUM, .enums = s_sd_22_v, .nenums = 4};
static const bim_attr_t t_sd_tva11_ColorType_a[] = {
  {.name = "type", .required = 1, .simple = &s_sd_22}
};
static const bim_type_t t_sd_tva11_ColorType = {.attrs = t_sd_tva11_ColorType_a, .nattrs = 1, .content = BIM_CONTENT_NONE, .name = "ColorType", .ns = ns_tva11, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_tva11_FrameRateType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "FrameRateType", .ns = ns_tva11, .base = &t_sd_xs_string};
static const bim_part_t t_sd_tva11_VideoAttributesType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Coding", .ns = ns_tva11, .type = &t_sd_tva11_ControlledTermType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Scan", .ns = ns_tva11, .type = &t_sd_tva11_ScanType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "HorizontalSize", .ns = ns_tva11, .type = &t_sd_xs_unsignedShort, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "VerticalSize", .ns = ns_tva11, .type = &t_sd_xs_unsignedShort, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "AspectRatio", .ns = ns_tva11, .type = &t_sd_tva11_AspectRatioType, .min = 0, .max = 2},
  {.kind = BIM_PART_ELEM, .name = "Color", .ns = ns_tva11, .type = &t_sd_tva11_ColorType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "FrameRate", .ns = ns_tva11, .type = &t_sd_tva11_FrameRateType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "BitRate", .ns = ns_tva11, .type = &t_sd_tva11_BitRateType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "PictureFormat", .ns = ns_tva11, .type = &t_sd_tva11_ControlledTermType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_tva11_VideoAttributesType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_tva11_VideoAttributesType_p, .nkids = 9};
static const bim_type_t t_sd_tva11_VideoAttributesType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_tva11_VideoAttributesType_top, .name = "VideoAttributesType", .ns = ns_tva11, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_PackageAvailabilityCountryCodeType_a[] = {
  {.name = "Availability", .simple = &s_sd_5}
};
static const bim_type_t t_sd_s08_PackageAvailabilityCountryCodeType = {.attrs = t_sd_s08_PackageAvailabilityCountryCodeType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "PackageAvailabilityCountryCodeType", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_part_t t_sd_s08_ServiceAvailabilityType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "CountryCode", .ns = ns_s08, .type = &t_sd_s08_PackageAvailabilityCountryCodeType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Cells", .ns = ns_s08, .type = &t_sd_xs_string, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s08_ServiceAvailabilityType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_ServiceAvailabilityType_p, .nkids = 2};
static const bim_type_t t_sd_s08_ServiceAvailabilityType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s08_ServiceAvailabilityType_top, .name = "ServiceAvailabilityType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const char *const s_sd_23_v[] = {"3D", "DSMService", "FCC", "HD", "Main", "PiP", "SD"};
static const bim_simple_t s_sd_23 = {.codec = BIM_ENUM, .enums = s_sd_23_v, .nenums = 7};
static const bim_type_t t_sd_s14_Usage = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_23, .name = "Usage", .ns = ns_s14, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s08_Hexadecimal8bit = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Hexadecimal8bit", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_xs_base64Binary = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_10, .name = "base64Binary", .ns = ns_xs, .base = &t_sd_xs_anySimpleType};
static const bim_part_t t_sd_s14_URILinkageType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "UriLinkageType", .ns = ns_s14, .type = &t_sd_s08_Hexadecimal8bit, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "URI", .ns = ns_s14, .type = &t_sd_xs_anyURI, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "MinPollingInterval", .ns = ns_s14, .type = &t_sd_xs_unsignedShort, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "PrivateDataBytes", .ns = ns_s14, .type = &t_sd_xs_base64Binary, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s14_URILinkageType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_URILinkageType_p, .nkids = 4};
static const bim_type_t t_sd_s14_URILinkageType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s14_URILinkageType_top, .name = "URILinkageType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s14_ciAncillaryDataType_top = {.kind = BIM_PART_ELEM, .name = "AncillaryDataBytes", .ns = ns_s14, .type = &t_sd_xs_base64Binary, .min = 1, .max = 1};
static const bim_type_t t_sd_s14_ciAncillaryDataType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s14_ciAncillaryDataType_top, .name = "ciAncillaryDataType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s14_IPService_p[] = {
  {.kind = BIM_PART_ELEM, .name = "ServiceLocation", .ns = ns_s14, .type = &t_sd_s14_ServiceLocation, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TextualIdentifier", .ns = ns_s14, .type = &t_sd_s08_TextualIdentifier, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s14, .type = &t_sd_s08_DVBTriplet, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "MaxBitrate", .ns = ns_s14, .type = &t_sd_xs_positiveInteger, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "SI", .ns = ns_s14, .type = &t_sd_s08_SI, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "AudioAttributes", .ns = ns_s14, .type = &t_sd_tva11_AudioAttributesType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "VideoAttributes", .ns = ns_s14, .type = &t_sd_tva11_VideoAttributesType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServiceAvailability", .ns = ns_s14, .type = &t_sd_s08_ServiceAvailabilityType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Usage", .ns = ns_s14, .type = &t_sd_s14_Usage, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "LinkedService", .ns = ns_s14, .type = &t_sd_s14_IPService, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "URILinkage", .ns = ns_s14, .type = &t_sd_s14_URILinkageType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "ciAncillaryData", .ns = ns_s14, .type = &t_sd_s14_ciAncillaryDataType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s14_IPService_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_IPService_p, .nkids = 12};
static const bim_type_t t_sd_s14_IPService = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s14_IPService_top, .name = "IPService", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s14_IPServiceList_p[] = {
  {.kind = BIM_PART_ELEM, .name = "ServicesDescriptionLocation", .ns = ns_s14, .type = &t_sd_s08_DescriptionLocationBCG, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "SingleService", .ns = ns_s14, .type = &t_sd_s14_IPService, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s14_IPServiceList_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_IPServiceList_p, .nkids = 2};
static const bim_type_t t_sd_s14_IPServiceList = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s14_IPServiceList_top, .name = "IPServiceList", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s14_BroadcastOffering_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s14_BroadcastOffering_top = {.kind = BIM_PART_ELEM, .name = "ServiceList", .ns = ns_s14, .type = &t_sd_s14_IPServiceList, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s14_BroadcastOffering = {.attrs = t_sd_s14_BroadcastOffering_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_BroadcastOffering_top, .name = "BroadcastOffering", .ns = ns_s14, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_s08_CoDOffering_Catalogue_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_CoDOffering_Catalogue_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Locator", .ns = ns_s08, .type = &t_sd_xs_anyURI, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s08_CoDOffering_Catalogue_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_CoDOffering_Catalogue_p, .nkids = 3};
static const bim_type_t t_sd_s08_CoDOffering_Catalogue = {.attrs = t_sd_s08_CoDOffering_Catalogue_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_CoDOffering_Catalogue_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_CoDOffering_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_CoDOffering_top = {.kind = BIM_PART_ELEM, .name = "Catalogue", .ns = ns_s08, .type = &t_sd_s08_CoDOffering_Catalogue, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_CoDOffering = {.attrs = t_sd_s08_CoDOffering_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_CoDOffering_top, .name = "CoDOffering", .ns = ns_s08, .base = &t_sd_s08_OfferingBase};
static const bim_type_t t_sd_tva11_TVAIDType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "TVAIDType", .ns = ns_tva11, .base = &t_sd_xs_string};
static const bim_attr_t t_sd_s12_DescriptionLocationBCG_a[] = {
  {.name = "preferred", .simple = &s_sd_5}
};
static const bim_type_t t_sd_s12_DescriptionLocationBCG = {.attrs = t_sd_s12_DescriptionLocationBCG_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "DescriptionLocationBCG", .ns = ns_s12, .base = &t_sd_tva11_TVAIDType};
static const bim_attr_t t_sd_s12_PackageTextualIdentifier_a[] = {
  {.name = "DomainName", .simple = &s_sd_0},
  {.name = "Priority", .simple = &s_sd_6},
  {.name = "ServiceName", .required = 1, .simple = &s_sd_0}
};
static const bim_type_t t_sd_s12_PackageTextualIdentifier = {.attrs = t_sd_s12_PackageTextualIdentifier_a, .nattrs = 3, .content = BIM_CONTENT_NONE, .name = "PackageTextualIdentifier", .ns = ns_s12, .base = &t_sd_s08_TextualIdentifier};
static const bim_attr_t t_sd_s12_DVBTriplet_a[] = {
  {.name = "OrigNetId", .required = 1, .simple = &s_sd_4},
  {.name = "ServiceId", .required = 1, .simple = &s_sd_4},
  {.name = "TSId", .required = 1, .simple = &s_sd_4},
  {.name = "TSIdWildcard", .simple = &s_sd_0}
};
static const bim_type_t t_sd_s12_DVBTriplet = {.attrs = t_sd_s12_DVBTriplet_a, .nattrs = 4, .content = BIM_CONTENT_NONE, .name = "DVBTriplet", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s12_PackagedServiceType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "TextualID", .ns = ns_s12, .type = &t_sd_s12_PackageTextualIdentifier, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s12, .type = &t_sd_s12_DVBTriplet, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "DescriptionLocation", .ns = ns_s12, .type = &t_sd_s12_DescriptionLocationBCG, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "LogicalChannelNumber", .ns = ns_s12, .type = &t_sd_xs_positiveInteger, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s12_PackagedServiceType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s12_PackagedServiceType_p, .nkids = 4};
static const bim_type_t t_sd_s12_PackagedServiceType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s12_PackagedServiceType_top, .name = "PackagedServiceType", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_Package_PackageReference_a[] = {
  {.name = "Id", .simple = &s_sd_0}
};
static const bim_type_t t_sd_s12_Package_PackageReference = {.attrs = t_sd_s12_Package_PackageReference_a, .nattrs = 1, .content = BIM_CONTENT_NONE, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_Package_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0},
  {.name = "Visible", .simple = &s_sd_5}
};
static const bim_part_t t_sd_s12_Package_p[] = {
  {.kind = BIM_PART_ELEM, .name = "PackageName", .ns = ns_s12, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CountryAvailability", .ns = ns_s12, .type = &t_sd_s08_CountryAvailability, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageDescription", .ns = ns_s12, .type = &t_sd_s12_DescriptionLocationBCG, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Service", .ns = ns_s12, .type = &t_sd_s12_PackagedServiceType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageReference", .ns = ns_s12, .type = &t_sd_s12_Package_PackageReference, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageAvailability", .ns = ns_s12, .type = &t_sd_s08_ServiceAvailabilityType, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s12_Package_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s12_Package_p, .nkids = 6};
static const bim_type_t t_sd_s12_Package = {.attrs = t_sd_s12_Package_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_Package_top, .name = "Package", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s14_Package_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0},
  {.name = "Visible", .simple = &s_sd_5}
};
static const bim_part_t t_sd_s14_Package_p_0[] = {
  {.kind = BIM_PART_ELEM, .name = "PackageName", .ns = ns_s12, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CountryAvailability", .ns = ns_s12, .type = &t_sd_s08_CountryAvailability, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageDescription", .ns = ns_s12, .type = &t_sd_s12_DescriptionLocationBCG, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Service", .ns = ns_s12, .type = &t_sd_s12_PackagedServiceType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageReference", .ns = ns_s12, .type = &t_sd_s12_Package_PackageReference, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageAvailability", .ns = ns_s12, .type = &t_sd_s08_ServiceAvailabilityType, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s14_Package_p_1[] = {
  {.kind = BIM_PART_ELEM, .name = "URILinkage", .ns = ns_s14, .type = &t_sd_s14_URILinkageType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "ciAncillaryData", .ns = ns_s14, .type = &t_sd_s14_ciAncillaryDataType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s14_Package_p[] = {
  {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_Package_p_0, .nkids = 6},
  {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_Package_p_1, .nkids = 2}
};
static const bim_part_t t_sd_s14_Package_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_Package_p, .nkids = 2};
static const bim_type_t t_sd_s14_Package = {.attrs = t_sd_s14_Package_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_Package_top, .name = "Package", .ns = ns_s14, .base = &t_sd_s12_Package};
static const bim_attr_t t_sd_s14_PackagedServices_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s14_PackagedServices_top = {.kind = BIM_PART_ELEM, .name = "Package", .ns = ns_s14, .type = &t_sd_s14_Package, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s14_PackagedServices = {.attrs = t_sd_s14_PackagedServices_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_PackagedServices_top, .name = "PackagedServices", .ns = ns_s14, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_s14_DSMMType_a[] = {
  {.name = "DSMMLocation", .required = 1, .simple = &s_sd_0},
  {.name = "LogoURI", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s14_DSMMType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "DSMMName", .ns = ns_s14, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "DSMMID", .ns = ns_s14, .type = &t_sd_xs_decimal, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s14, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s14_DSMMType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_DSMMType_p, .nkids = 3};
static const bim_type_t t_sd_s14_DSMMType = {.attrs = t_sd_s14_DSMMType_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_DSMMType_top, .name = "DSMMType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const char *const s_sd_24_v[] = {"1 SAP", "2 DVBSTP"};
static const bim_simple_t s_sd_24 = {.codec = BIM_ENUM, .enums = s_sd_24_v, .nenums = 2};
static const bim_attr_t t_sd_s08_RMSFUSMulticastAddressType_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_2},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Protocol", .simple = &s_sd_24},
  {.name = "Source", .simple = &s_sd_2}
};
static const bim_type_t t_sd_s08_RMSFUSMulticastAddressType = {.attrs = t_sd_s08_RMSFUSMulticastAddressType_a, .nattrs = 4, .content = BIM_CONTENT_NONE, .name = "RMSFUSMulticastAddressType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s08_FUSAnnouncementType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "FUSAnnouncementDescription", .ns = ns_s08, .type = &t_sd_xs_string, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "MulticastAnnouncementAddress", .ns = ns_s08, .type = &t_sd_s08_RMSFUSMulticastAddressType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "FUSUnicastAnnouncement", .ns = ns_s08, .type = &t_sd_xs_anyURI, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "QRCLocation", .ns = ns_s08, .type = &t_sd_xs_anyURI, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s08_FUSAnnouncementType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_FUSAnnouncementType_p, .nkids = 4};
static const bim_type_t t_sd_s08_FUSAnnouncementType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s08_FUSAnnouncementType_top, .name = "FUSAnnouncementType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_FUSType_a[] = {
  {.name = "LogoURI", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_FUSType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "FUSName", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "FUSID", .ns = ns_s08, .type = &t_sd_xs_decimal, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "FUSAnnouncement", .ns = ns_s08, .type = &t_sd_s08_FUSAnnouncementType, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s08_FUSType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_FUSType_p, .nkids = 4};
static const bim_type_t t_sd_s08_FUSType = {.attrs = t_sd_s08_FUSType_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_FUSType_top, .name = "FUSType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s14_RMSFUSMulticastAddressType_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_3},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Protocol", .simple = &s_sd_24},
  {.name = "Source", .simple = &s_sd_3}
};
static const bim_type_t t_sd_s14_RMSFUSMulticastAddressType = {.attrs = t_sd_s14_RMSFUSMulticastAddressType_a, .nattrs = 4, .content = BIM_CONTENT_NONE, .name = "RMSFUSMulticastAddressType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s14_FUSAnnouncementType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "FUSAnnouncementDescription", .ns = ns_s14, .type = &t_sd_xs_string, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "MulticastAnnouncementAddress", .ns = ns_s14, .type = &t_sd_s14_RMSFUSMulticastAddressType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "FUSUnicastAnnouncement", .ns = ns_s14, .type = &t_sd_xs_anyURI, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "QRCLocation", .ns = ns_s14, .type = &t_sd_xs_anyURI, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s14_FUSAnnouncementType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_FUSAnnouncementType_p, .nkids = 4};
static const bim_type_t t_sd_s14_FUSAnnouncementType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s14_FUSAnnouncementType_top, .name = "FUSAnnouncementType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s14_FUSType_a[] = {
  {.name = "LogoURI", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s14_FUSType_p_0[] = {
  {.kind = BIM_PART_ELEM, .name = "FUSName", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "FUSID", .ns = ns_s08, .type = &t_sd_xs_decimal, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "FUSAnnouncement", .ns = ns_s08, .type = &t_sd_s08_FUSAnnouncementType, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s14_FUSType_p[] = {
  {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_FUSType_p_0, .nkids = 4},
  {.kind = BIM_PART_ELEM, .name = "FUSAnnouncement", .ns = ns_s14, .type = &t_sd_s14_FUSAnnouncementType, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s14_FUSType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_FUSType_p, .nkids = 2};
static const bim_type_t t_sd_s14_FUSType = {.attrs = t_sd_s14_FUSType_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_FUSType_top, .name = "FUSType", .ns = ns_s14, .base = &t_sd_s08_FUSType};
static const bim_attr_t t_sd_s08_RMSType_a[] = {
  {.name = "LogoURI", .simple = &s_sd_0},
  {.name = "RMSLocation", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_RMSType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "RMSName", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "RMSID", .ns = ns_s08, .type = &t_sd_xs_decimal, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s08_RMSType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_RMSType_p, .nkids = 3};
static const bim_type_t t_sd_s08_RMSType = {.attrs = t_sd_s08_RMSType_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_RMSType_top, .name = "RMSType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s14_RMSFUSDiscoveryType_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s14_RMSFUSDiscoveryType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "DSMProvider", .ns = ns_s14, .type = &t_sd_s14_DSMMType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "FUSProvider", .ns = ns_s14, .type = &t_sd_s14_FUSType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "RMSProvider", .ns = ns_s14, .type = &t_sd_s08_RMSType, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s14_RMSFUSDiscoveryType_top = {.kind = BIM_PART_CHOICE, .min = 0, .max = 1, .kids = t_sd_s14_RMSFUSDiscoveryType_p, .nkids = 3};
static const bim_type_t t_sd_s14_RMSFUSDiscoveryType = {.attrs = t_sd_s14_RMSFUSDiscoveryType_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_RMSFUSDiscoveryType_top, .name = "RMSFUSDiscoveryType", .ns = ns_s14, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_s08_CivicAddress_a[] = {
  {.name = "Type", .required = 1, .simple = &s_sd_0},
  {.name = "Value", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_CivicAddress_top = {.kind = BIM_PART_ELEM, .name = "CA", .ns = ns_s08, .type = &t_sd_s08_CivicAddress, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_CivicAddress = {.attrs = t_sd_s08_CivicAddress_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_CivicAddress_top, .name = "CivicAddress", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_Cell_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_Cell_p[] = {
  {.kind = BIM_PART_ELEM, .name = "CountryCode", .ns = ns_s08, .type = &t_sd_xs_string, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "CA", .ns = ns_s08, .type = &t_sd_s08_CivicAddress, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s08_Cell_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_Cell_p, .nkids = 2};
static const bim_type_t t_sd_s08_Cell = {.attrs = t_sd_s08_Cell_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_Cell_top, .name = "Cell", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_RegionalizationOffering_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_RegionalizationOffering_top = {.kind = BIM_PART_ELEM, .name = "Cell", .ns = ns_s08, .type = &t_sd_s08_Cell, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_RegionalizationOffering = {.attrs = t_sd_s08_RegionalizationOffering_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_RegionalizationOffering_top, .name = "RegionalizationOffering", .ns = ns_s08, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_s12_SRMIDType_a[] = {
  {.name = "CPSystemID", .required = 1, .simple = &s_sd_0},
  {.name = "CPSystemSRMID", .simple = &s_sd_0}
};
static const bim_type_t t_sd_s12_SRMIDType = {.attrs = t_sd_s12_SRMIDType_a, .nattrs = 2, .content = BIM_CONTENT_NONE, .name = "SRMIDType", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s14_SRMAnnouncementModeSAPType_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_3},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_3}
};
static const bim_type_t t_sd_s14_SRMAnnouncementModeSAPType = {.attrs = t_sd_s14_SRMAnnouncementModeSAPType_a, .nattrs = 3, .content = BIM_CONTENT_NONE, .name = "SRMAnnouncementModeSAPType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s14_SRMAnnouncementModeType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "HTTP", .ns = ns_s14, .type = &t_sd_xs_anyURI, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "SAP", .ns = ns_s14, .type = &t_sd_s14_SRMAnnouncementModeSAPType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s14_SRMAnnouncementModeType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = 1, .kids = t_sd_s14_SRMAnnouncementModeType_p, .nkids = 2};
static const bim_type_t t_sd_s14_SRMAnnouncementModeType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s14_SRMAnnouncementModeType_top, .name = "SRMAnnouncementModeType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s14_SRMAnnouncementServiceType_a[] = {
  {.name = "AnnouncementServiceVersion", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s14_SRMAnnouncementServiceType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "SRMID", .ns = ns_s14, .type = &t_sd_s12_SRMIDType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "SRMAnnouncementMode", .ns = ns_s14, .type = &t_sd_s14_SRMAnnouncementModeType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s14_SRMAnnouncementServiceType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_SRMAnnouncementServiceType_p, .nkids = 2};
static const bim_type_t t_sd_s14_SRMAnnouncementServiceType = {.attrs = t_sd_s14_SRMAnnouncementServiceType_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_SRMAnnouncementServiceType_top, .name = "SRMAnnouncementServiceType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_SRMIDVerMType_a[] = {
  {.name = "CPSystemID", .required = 1, .simple = &s_sd_0},
  {.name = "CPSystemSRMID", .simple = &s_sd_0},
  {.name = "SRMFileVersion", .simple = &s_sd_0}
};
static const bim_type_t t_sd_s12_SRMIDVerMType = {.attrs = t_sd_s12_SRMIDVerMType_a, .nattrs = 3, .content = BIM_CONTENT_NONE, .name = "SRMIDVerMType", .ns = ns_s12, .base = &t_sd_s12_SRMIDType};
static const bim_attr_t t_sd_s14_SRMDownloadServiceFLUTEType_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_3},
  {.name = "FLUTESessionVersion", .simple = &s_sd_0},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_3},
  {.name = "TSI", .required = 1, .simple = &s_sd_4}
};
static const bim_part_t t_sd_s14_SRMDownloadServiceFLUTEType_top = {.kind = BIM_PART_ELEM, .name = "SRMIDVer", .ns = ns_s14, .type = &t_sd_s12_SRMIDVerMType, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s14_SRMDownloadServiceFLUTEType = {.attrs = t_sd_s14_SRMDownloadServiceFLUTEType_a, .nattrs = 5, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_SRMDownloadServiceFLUTEType_top, .name = "SRMDownloadServiceFLUTEType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_SRMIDVerUType_a[] = {
  {.name = "CPSystemID", .required = 1, .simple = &s_sd_0},
  {.name = "CPSystemSRMID", .simple = &s_sd_0},
  {.name = "SRMFileVersion", .required = 1, .simple = &s_sd_0}
};
static const bim_type_t t_sd_s12_SRMIDVerUType = {.attrs = t_sd_s12_SRMIDVerUType_a, .nattrs = 3, .content = BIM_CONTENT_NONE, .name = "SRMIDVerUType", .ns = ns_s12, .base = &t_sd_s12_SRMIDType};
static const bim_attr_t t_sd_s12_SRMDownloadServiceHTTPType_a[] = {
  {.name = "Location", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_SRMDownloadServiceHTTPType_top = {.kind = BIM_PART_ELEM, .name = "SRMIDVer", .ns = ns_s12, .type = &t_sd_s12_SRMIDVerUType, .min = 1, .max = 1};
static const bim_type_t t_sd_s12_SRMDownloadServiceHTTPType = {.attrs = t_sd_s12_SRMDownloadServiceHTTPType_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_SRMDownloadServiceHTTPType_top, .name = "SRMDownloadServiceHTTPType", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s14_SRMDownloadServiceType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "FLUTE", .ns = ns_s14, .type = &t_sd_s14_SRMDownloadServiceFLUTEType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "HTTP", .ns = ns_s14, .type = &t_sd_s12_SRMDownloadServiceHTTPType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s14_SRMDownloadServiceType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = 1, .kids = t_sd_s14_SRMDownloadServiceType_p, .nkids = 2};
static const bim_type_t t_sd_s14_SRMDownloadServiceType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s14_SRMDownloadServiceType_top, .name = "SRMDownloadServiceType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s14_SRMOffering_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s14_SRMOffering_p[] = {
  {.kind = BIM_PART_ELEM, .name = "SRMAnnouncementService", .ns = ns_s14, .type = &t_sd_s14_SRMAnnouncementServiceType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "SRMDownloadService", .ns = ns_s14, .type = &t_sd_s14_SRMDownloadServiceType, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s14_SRMOffering_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_SRMOffering_p, .nkids = 2};
static const bim_type_t t_sd_s14_SRMOffering = {.attrs = t_sd_s14_SRMOffering_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_SRMOffering_top, .name = "SRMOffering", .ns = ns_s14, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_s14_OfferingListType_Pull_a[] = {
  {.name = "Location", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s14_OfferingListType_Pull_top = {.kind = BIM_PART_ELEM, .name = "PayloadId", .ns = ns_s12, .type = &t_sd_s12_PayloadList_PayloadId, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s14_OfferingListType_Pull = {.attrs = t_sd_s14_OfferingListType_Pull_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_OfferingListType_Pull_top, .base = &t_sd_s12_PayloadList};
static const bim_part_t t_sd_s14_OfferingListType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Pull", .ns = ns_s14, .type = &t_sd_s14_OfferingListType_Pull, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Push", .ns = ns_s14, .type = &t_sd_s14_DVBSTPTransportModeType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s14_OfferingListType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = BIM_UNBOUNDED, .kids = t_sd_s14_OfferingListType_p, .nkids = 2};
static const bim_type_t t_sd_s14_OfferingListType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s14_OfferingListType_top, .name = "OfferingListType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s14_ServiceProviderType_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "LogoURI", .simple = &s_sd_0},
  {.name = "Version", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s14_ServiceProviderType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_s14, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s14, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Offering", .ns = ns_s14, .type = &t_sd_s14_OfferingListType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s14_ServiceProviderType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s14_ServiceProviderType_p, .nkids = 3};
static const bim_type_t t_sd_s14_ServiceProviderType = {.attrs = t_sd_s14_ServiceProviderType_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_s14_ServiceProviderType_top, .name = "ServiceProviderType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s14_ServiceProviderListType_top = {.kind = BIM_PART_ELEM, .name = "ServiceProvider", .ns = ns_s14, .type = &t_sd_s14_ServiceProviderType, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s14_ServiceProviderListType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s14_ServiceProviderListType_top, .name = "ServiceProviderListType", .ns = ns_s14, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_ReferencedServiceProviderType_Service_a[] = {
  {.name = "Name", .required = 1, .simple = &s_sd_0}
};
static const bim_type_t t_sd_s12_ReferencedServiceProviderType_Service = {.attrs = t_sd_s12_ReferencedServiceProviderType_Service_a, .nattrs = 1, .content = BIM_CONTENT_NONE, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_ReferencedServiceProviderType_a[] = {
  {.name = "Domain", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_ReferencedServiceProviderType_top = {.kind = BIM_PART_ELEM, .name = "Service", .ns = ns_s12, .type = &t_sd_s12_ReferencedServiceProviderType_Service, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s12_ReferencedServiceProviderType = {.attrs = t_sd_s12_ReferencedServiceProviderType_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_ReferencedServiceProviderType_top, .name = "ReferencedServiceProviderType", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_ReferencedServices_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_ReferencedServices_top = {.kind = BIM_PART_ELEM, .name = "ReferencedServiceProvider", .ns = ns_s12, .type = &t_sd_s12_ReferencedServiceProviderType, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s12_ReferencedServices = {.attrs = t_sd_s12_ReferencedServices_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_ReferencedServices_top, .name = "ReferencedServices", .ns = ns_s12, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_ServiceDiscovery_a[] = {
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_ServiceDiscovery_p[] = {
  {.kind = BIM_PART_ELEM, .name = "BCGDiscovery", .ns = ns_s14, .type = &t_sd_s14_BCGOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "BroadcastDiscovery", .ns = ns_s14, .type = &t_sd_s14_BroadcastOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CoDDiscovery", .ns = ns_s14, .type = &t_sd_s08_CoDOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageDiscovery", .ns = ns_s14, .type = &t_sd_s14_PackagedServices, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "RMSFUSDiscovery", .ns = ns_s14, .type = &t_sd_s14_RMSFUSDiscoveryType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "RegionalizationDiscovery", .ns = ns_s14, .type = &t_sd_s08_RegionalizationOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "SRMDiscovery", .ns = ns_s14, .type = &t_sd_s14_SRMOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServiceProviderDiscovery", .ns = ns_s14, .type = &t_sd_s14_ServiceProviderListType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServicesFromOtherSP", .ns = ns_s14, .type = &t_sd_s12_ReferencedServices, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_ServiceDiscovery_top = {.kind = BIM_PART_CHOICE, .min = 0, .max = 1, .kids = t_sd_ServiceDiscovery_p, .nkids = 9};
static const bim_type_t t_sd_ServiceDiscovery = {.attrs = t_sd_ServiceDiscovery_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_ServiceDiscovery_top, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_s08_PullURL = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "PullURL", .ns = ns_s08, .base = &t_sd_xs_anyURI};
static const bim_type_t t_sd_tva05_CRIDType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "CRIDType", .ns = ns_tva05, .base = &t_sd_xs_anyURI};
static const bim_type_t t_sd_tva05_InstanceMetadataIdType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "InstanceMetadataIdType", .ns = ns_tva05, .base = &t_sd_xs_anyURI};
static const bim_type_t t_sd_tva11_CRIDType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "CRIDType", .ns = ns_tva11, .base = &t_sd_xs_anyURI};
static const bim_type_t t_sd_tva11_InstanceMetadataIdType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "InstanceMetadataIdType", .ns = ns_tva11, .base = &t_sd_xs_anyURI};
static const bim_simple_t s_sd_25 = {.codec = BIM_RANGE, .range = {INT64_C(0), UINT64_C(255)}};
static const bim_type_t t_sd_xs_unsignedByte = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_25, .name = "unsignedByte", .ns = ns_xs, .base = &t_sd_xs_unsignedShort};
static const bim_type_t t_sd_s08_Integer6bit = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_17, .name = "Integer6bit", .ns = ns_s08, .base = &t_sd_xs_unsignedShort};
static const bim_type_t t_sd_s08_OrigNetId = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_4, .name = "OrigNetId", .ns = ns_s08, .base = &t_sd_xs_unsignedShort};
static const bim_type_t t_sd_s08_ServiceId = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_4, .name = "ServiceId", .ns = ns_s08, .base = &t_sd_xs_unsignedShort};
static const bim_type_t t_sd_s08_TSId = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_4, .name = "TSId", .ns = ns_s08, .base = &t_sd_xs_unsignedShort};
static const bim_attr_t t_sd_tva05_BitRateType_a[] = {
  {.name = "average", .simple = &s_sd_9},
  {.name = "maximum", .simple = &s_sd_9},
  {.name = "minimum", .simple = &s_sd_9},
  {.name = "variable", .simple = &s_sd_5}
};
static const bim_type_t t_sd_tva05_BitRateType = {.attrs = t_sd_tva05_BitRateType_a, .nattrs = 4, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "BitRateType", .ns = ns_tva05, .base = &t_sd_xs_nonNegativeInteger};
static const bim_simple_t s_sd_26 = {.codec = BIM_RANGE, .range = {INT64_C(0), UINT64_C(4095)}};
static const bim_type_t t_sd_mpeg7_unsigned12 = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_26, .name = "unsigned12", .ns = ns_mpeg7, .base = &t_sd_xs_nonNegativeInteger};
static const bim_type_t t_sd_mpeg7_unsigned8 = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_25, .name = "unsigned8", .ns = ns_mpeg7, .base = &t_sd_xs_nonNegativeInteger};
static const bim_type_t t_sd_mpeg7_08_unsigned12 = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_26, .name = "unsigned12", .ns = ns_mpeg7_08, .base = &t_sd_xs_nonNegativeInteger};
static const bim_type_t t_sd_mpeg7_08_unsigned8 = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_25, .name = "unsigned8", .ns = ns_mpeg7_08, .base = &t_sd_xs_nonNegativeInteger};
static const bim_type_t t_sd_xs_nonPositiveInteger = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "nonPositiveInteger", .ns = ns_xs, .base = &t_sd_xs_integer};
static const bim_type_t t_sd_xs_negativeInteger = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "negativeInteger", .ns = ns_xs, .base = &t_sd_xs_nonPositiveInteger};
static const bim_attr_t t_sd_mpeg7_IncrDurationType_a[] = {
  {.name = "timeUnit", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_IncrDurationType = {.attrs = t_sd_mpeg7_IncrDurationType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "IncrDurationType", .ns = ns_mpeg7, .base = &t_sd_xs_integer};
static const bim_attr_t t_sd_mpeg7_MediaIncrDurationType_a[] = {
  {.name = "mediaTimeUnit", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_MediaIncrDurationType = {.attrs = t_sd_mpeg7_MediaIncrDurationType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "MediaIncrDurationType", .ns = ns_mpeg7, .base = &t_sd_xs_integer};
static const bim_attr_t t_sd_mpeg7_MediaRelIncrTimePointType_a[] = {
  {.name = "mediaTimeUnit", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_MediaRelIncrTimePointType = {.attrs = t_sd_mpeg7_MediaRelIncrTimePointType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "MediaRelIncrTimePointType", .ns = ns_mpeg7, .base = &t_sd_xs_integer};
static const bim_attr_t t_sd_tva05_TVAMediaRelIncrTimePointType_a[] = {
  {.name = "mediaTimeUnit", .simple = &s_sd_0}
};
static const bim_type_t t_sd_tva05_TVAMediaRelIncrTimePointType = {.attrs = t_sd_tva05_TVAMediaRelIncrTimePointType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "TVAMediaRelIncrTimePointType", .ns = ns_tva05, .base = &t_sd_mpeg7_MediaRelIncrTimePointType};
static const bim_attr_t t_sd_mpeg7_RelIncrTimePointType_a[] = {
  {.name = "timeBase", .simple = &s_sd_0},
  {.name = "timeUnit", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_RelIncrTimePointType = {.attrs = t_sd_mpeg7_RelIncrTimePointType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "RelIncrTimePointType", .ns = ns_mpeg7, .base = &t_sd_xs_integer};
static const bim_simple_t s_sd_27 = {.codec = BIM_RANGE, .range = {INT64_C(-100), UINT64_C(200)}};
static const bim_type_t t_sd_mpeg7_preferenceValueType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_27, .name = "preferenceValueType", .ns = ns_mpeg7, .base = &t_sd_xs_integer};
static const bim_attr_t t_sd_mpeg7_08_IncrDurationType_a[] = {
  {.name = "timeUnit", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_08_IncrDurationType = {.attrs = t_sd_mpeg7_08_IncrDurationType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "IncrDurationType", .ns = ns_mpeg7_08, .base = &t_sd_xs_integer};
static const bim_attr_t t_sd_mpeg7_08_MediaIncrDurationType_a[] = {
  {.name = "mediaTimeUnit", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_08_MediaIncrDurationType = {.attrs = t_sd_mpeg7_08_MediaIncrDurationType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "MediaIncrDurationType", .ns = ns_mpeg7_08, .base = &t_sd_xs_integer};
static const bim_attr_t t_sd_mpeg7_08_MediaRelIncrTimePointType_a[] = {
  {.name = "mediaTimeUnit", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_08_MediaRelIncrTimePointType = {.attrs = t_sd_mpeg7_08_MediaRelIncrTimePointType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "MediaRelIncrTimePointType", .ns = ns_mpeg7_08, .base = &t_sd_xs_integer};
static const bim_attr_t t_sd_tva11_TVAMediaRelIncrTimePointType_a[] = {
  {.name = "mediaTimeUnit", .simple = &s_sd_0}
};
static const bim_type_t t_sd_tva11_TVAMediaRelIncrTimePointType = {.attrs = t_sd_tva11_TVAMediaRelIncrTimePointType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "TVAMediaRelIncrTimePointType", .ns = ns_tva11, .base = &t_sd_mpeg7_08_MediaRelIncrTimePointType};
static const bim_attr_t t_sd_mpeg7_08_RelIncrTimePointType_a[] = {
  {.name = "timeBase", .simple = &s_sd_0},
  {.name = "timeUnit", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_08_RelIncrTimePointType = {.attrs = t_sd_mpeg7_08_RelIncrTimePointType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_6, .name = "RelIncrTimePointType", .ns = ns_mpeg7_08, .base = &t_sd_xs_integer};
static const bim_type_t t_sd_mpeg7_08_preferenceValueType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_27, .name = "preferenceValueType", .ns = ns_mpeg7_08, .base = &t_sd_xs_integer};
static const bim_type_t t_sd_xs_NMTOKEN = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "NMTOKEN", .ns = ns_xs, .base = &t_sd_xs_token};
static const char *const s_sd_28_v[] = {"false", "true", "user"};
static const bim_simple_t s_sd_28 = {.codec = BIM_ENUM, .enums = s_sd_28_v, .nenums = 3};
static const bim_type_t t_sd_mpeg7_userChoiceType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_28, .name = "userChoiceType", .ns = ns_mpeg7, .base = &t_sd_xs_NMTOKEN};
static const bim_type_t t_sd_mpeg7_08_userChoiceType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_28, .name = "userChoiceType", .ns = ns_mpeg7_08, .base = &t_sd_xs_NMTOKEN};
static const bim_type_t t_sd_xs_Name = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Name", .ns = ns_xs, .base = &t_sd_xs_token};
static const bim_type_t t_sd_xs_NCName = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "NCName", .ns = ns_xs, .base = &t_sd_xs_Name};
static const bim_type_t t_sd_xs_ENTITY = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ENTITY", .ns = ns_xs, .base = &t_sd_xs_NCName};
static const bim_type_t t_sd_xs_ID = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ID", .ns = ns_xs, .base = &t_sd_xs_NCName};
static const bim_type_t t_sd_xs_IDREF = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "IDREF", .ns = ns_xs, .base = &t_sd_xs_NCName};
static const bim_attr_t t_sd_tva05_CaptionLanguageType_a[] = {
  {.name = "closed", .simple = &s_sd_5},
  {.name = "supplemental", .simple = &s_sd_5}
};
static const bim_type_t t_sd_tva05_CaptionLanguageType = {.attrs = t_sd_tva05_CaptionLanguageType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "CaptionLanguageType", .ns = ns_tva05, .base = &t_sd_xs_language};
static const bim_attr_t t_sd_tva05_SignLanguageType_a[] = {
  {.name = "primary", .simple = &s_sd_5},
  {.name = "translation", .simple = &s_sd_5},
  {.name = "type", .simple = &s_sd_0}
};
static const bim_type_t t_sd_tva05_SignLanguageType = {.attrs = t_sd_tva05_SignLanguageType_a, .nattrs = 3, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "SignLanguageType", .ns = ns_tva05, .base = &t_sd_xs_language};
static const bim_attr_t t_sd_tva11_CaptionLanguageType_a[] = {
  {.name = "closed", .simple = &s_sd_5},
  {.name = "primary", .simple = &s_sd_5},
  {.name = "supplemental", .simple = &s_sd_5},
  {.name = "translation", .simple = &s_sd_5}
};
static const bim_type_t t_sd_tva11_CaptionLanguageType = {.attrs = t_sd_tva11_CaptionLanguageType_a, .nattrs = 4, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "CaptionLanguageType", .ns = ns_tva11, .base = &t_sd_xs_language};
static const bim_attr_t t_sd_tva11_SignLanguageType_a[] = {
  {.name = "closed", .simple = &s_sd_5},
  {.name = "primary", .simple = &s_sd_5},
  {.name = "translation", .simple = &s_sd_5},
  {.name = "type", .simple = &s_sd_0}
};
static const bim_type_t t_sd_tva11_SignLanguageType = {.attrs = t_sd_tva11_SignLanguageType_a, .nattrs = 4, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "SignLanguageType", .ns = ns_tva11, .base = &t_sd_xs_language};
static const bim_attr_t t_sd_mpeg7_ExtendedLanguageType_a[] = {
  {.name = "supplemental", .simple = &s_sd_5},
  {.name = "type", .simple = &s_sd_19}
};
static const bim_type_t t_sd_mpeg7_ExtendedLanguageType = {.attrs = t_sd_mpeg7_ExtendedLanguageType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ExtendedLanguageType", .ns = ns_mpeg7, .base = &t_sd_xs_language};
static const bim_attr_t t_sd_tva05_AudioLanguageType_a[] = {
  {.name = "purpose", .simple = &s_sd_2},
  {.name = "supplemental", .simple = &s_sd_5},
  {.name = "type", .simple = &s_sd_19}
};
static const bim_type_t t_sd_tva05_AudioLanguageType = {.attrs = t_sd_tva05_AudioLanguageType_a, .nattrs = 3, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "AudioLanguageType", .ns = ns_tva05, .base = &t_sd_mpeg7_ExtendedLanguageType};
static const bim_type_t t_sd_mpeg7_xPathType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "xPathType", .ns = ns_mpeg7, .base = &t_sd_xs_token};
static const bim_type_t t_sd_mpeg7_xPathRefType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "xPathRefType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_xPathType};
static const bim_type_t t_sd_mpeg7_xPathSelectorType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "xPathSelectorType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_xPathType};
static const bim_type_t t_sd_mpeg7_xPathAbsoluteSelectorType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "xPathAbsoluteSelectorType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_xPathSelectorType};
static const bim_type_t t_sd_mpeg7_08_xPathType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "xPathType", .ns = ns_mpeg7_08, .base = &t_sd_xs_token};
static const bim_type_t t_sd_mpeg7_08_xPathRefType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "xPathRefType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_xPathType};
static const bim_type_t t_sd_mpeg7_08_xPathSelectorType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "xPathSelectorType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_xPathType};
static const bim_type_t t_sd_mpeg7_08_xPathAbsoluteSelectorType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "xPathAbsoluteSelectorType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_xPathSelectorType};
static const bim_type_t t_sd_s08_Hexadecimal32bit = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Hexadecimal32bit", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s08_Hexadecimal3bit = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Hexadecimal3bit", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s08_Hexadecimal4bit = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Hexadecimal4bit", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s08_ServiceType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ServiceType", .ns = ns_s08, .base = &t_sd_s08_Hexadecimal8bit};
static const bim_type_t t_sd_s08_IPType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "IPType", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s08_ISO_3166_List = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ISO-3166-List", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s08_ISO639_2 = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ISO639-2", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s08_PrimarySISource = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_18, .name = "PrimarySISource", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s08_Service = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Service", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s08_StreamingType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_11, .name = "StreamingType", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s08_TransportProtocolType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_8, .name = "TransportProtocolType", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s08_Version = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Version", .ns = ns_s08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s12_CPSystemIDType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "CPSystemIDType", .ns = ns_s12, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s12_CPSystemSRMIDType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "CPSystemSRMIDType", .ns = ns_s12, .base = &t_sd_xs_string};
static const char *const s_sd_29_v[] = {"FCC", "RET"};
static const bim_simple_t s_sd_29 = {.codec = BIM_ENUM, .enums = s_sd_29_v, .nenums = 2};
static const bim_type_t t_sd_s12_EnhancementServiceType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_29, .name = "EnhancementServiceType", .ns = ns_s12, .base = &t_sd_xs_string};
static const char *const s_sd_30_v[] = {"3D", "FCC", "HD", "Main", "PiP", "SD"};
static const bim_simple_t s_sd_30 = {.codec = BIM_ENUM, .enums = s_sd_30_v, .nenums = 6};
static const bim_type_t t_sd_s12_Usage = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_30, .name = "Usage", .ns = ns_s12, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s14_IPv4Type = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "IPv4Type", .ns = ns_s14, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s14_IPv6Type = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "IPv6Type", .ns = ns_s14, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva05_ColorTypeType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_22, .name = "ColorTypeType", .ns = ns_tva05, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva05_FrameRateType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "FrameRateType", .ns = ns_tva05, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva05_RatioType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "RatioType", .ns = ns_tva05, .base = &t_sd_xs_string};
static const bim_attr_t t_sd_tva05_AspectRatioType_a[] = {
  {.name = "type", .simple = &s_sd_21}
};
static const bim_type_t t_sd_tva05_AspectRatioType = {.attrs = t_sd_tva05_AspectRatioType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "AspectRatioType", .ns = ns_tva05, .base = &t_sd_tva05_RatioType};
static const bim_type_t t_sd_tva05_ScanType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_20, .name = "ScanType", .ns = ns_tva05, .base = &t_sd_xs_string};
static const char *const s_sd_31_v[] = {"long", "medium", "short"};
static const bim_simple_t s_sd_31 = {.codec = BIM_ENUM, .enums = s_sd_31_v, .nenums = 3};
static const bim_type_t t_sd_tva05_SynopsisLengthType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_31, .name = "SynopsisLengthType", .ns = ns_tva05, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva05_TVAIDRefType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "TVAIDRefType", .ns = ns_tva05, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva05_TVAIDType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "TVAIDType", .ns = ns_tva05, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva05_currencyCodeType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "currencyCodeType", .ns = ns_tva05, .base = &t_sd_xs_string};
static const char *const s_sd_32_v[] = {"segment", "segmentgroup"};
static const bim_simple_t s_sd_32 = {.codec = BIM_ENUM, .enums = s_sd_32_v, .nenums = 2};
static const bim_type_t t_sd_tva05_segmentTypeType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_32, .name = "segmentTypeType", .ns = ns_tva05, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva05_serviceInformationNameLengthType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_31, .name = "serviceInformationNameLengthType", .ns = ns_tva05, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva11_ColorTypeType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_22, .name = "ColorTypeType", .ns = ns_tva11, .base = &t_sd_xs_string};
static const char *const s_sd_33_v[] = {"download", "streaming"};
static const bim_simple_t s_sd_33 = {.codec = BIM_ENUM, .enums = s_sd_33_v, .nenums = 2};
static const bim_type_t t_sd_tva11_DeliveryModeType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_33, .name = "DeliveryModeType", .ns = ns_tva11, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva11_ExplanationLengthType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_31, .name = "ExplanationLengthType", .ns = ns_tva11, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva11_SynopsisLengthType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_31, .name = "SynopsisLengthType", .ns = ns_tva11, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva11_TVAIDRefType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "TVAIDRefType", .ns = ns_tva11, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva11_currencyCodeType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "currencyCodeType", .ns = ns_tva11, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva11_segmentTypeType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_32, .name = "segmentTypeType", .ns = ns_tva11, .base = &t_sd_xs_string};
static const bim_type_t t_sd_tva11_serviceInformationNameLengthType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_31, .name = "serviceInformationNameLengthType", .ns = ns_tva11, .base = &t_sd_xs_string};
static const bim_attr_t t_sd_mpeg7_TextualBaseType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_TextualBaseType = {.attrs = t_sd_mpeg7_TextualBaseType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "TextualBaseType", .ns = ns_mpeg7, .base = &t_sd_xs_string, .abstract = 1};
static const bim_attr_t t_sd_mpeg7_NameComponentType_a[] = {
  {.name = "abbrev", .simple = &s_sd_0},
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "initial", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_NameComponentType = {.attrs = t_sd_mpeg7_NameComponentType_a, .nattrs = 3, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "NameComponentType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_TextualBaseType};
static const bim_attr_t t_sd_mpeg7_TextualType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_TextualType = {.attrs = t_sd_mpeg7_TextualType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "TextualType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_TextualBaseType};
static const char *const s_sd_34_v[] = {"main", "other", "secondary"};
static const bim_simple_t s_sd_34 = {.codec = BIM_ENUM, .enums = s_sd_34_v, .nenums = 3};
static const bim_attr_t t_sd_tva05_KeywordType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "metadataOriginIDRef", .simple = &s_sd_0},
  {.name = "type", .simple = &s_sd_34}
};
static const bim_type_t t_sd_tva05_KeywordType = {.attrs = t_sd_tva05_KeywordType_a, .nattrs = 3, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "KeywordType", .ns = ns_tva05, .base = &t_sd_mpeg7_TextualType};
static const bim_attr_t t_sd_tva05_OrganizationNameType_a[] = {
  {.name = "fragmentId", .simple = &s_sd_0},
  {.name = "fragmentVersion", .simple = &s_sd_9},
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "metadataOriginIDRef", .simple = &s_sd_0},
  {.name = "organizationNameId", .required = 1, .simple = &s_sd_0}
};
static const bim_type_t t_sd_tva05_OrganizationNameType = {.attrs = t_sd_tva05_OrganizationNameType_a, .nattrs = 5, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "OrganizationNameType", .ns = ns_tva05, .base = &t_sd_mpeg7_TextualType};
static const bim_attr_t t_sd_tva05_ServiceInformationNameType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "length", .simple = &s_sd_31}
};
static const bim_type_t t_sd_tva05_ServiceInformationNameType = {.attrs = t_sd_tva05_ServiceInformationNameType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ServiceInformationNameType", .ns = ns_tva05, .base = &t_sd_mpeg7_TextualType};
static const bim_attr_t t_sd_tva05_SynopsisType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "length", .simple = &s_sd_31}
};
static const bim_type_t t_sd_tva05_SynopsisType = {.attrs = t_sd_tva05_SynopsisType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "SynopsisType", .ns = ns_tva05, .base = &t_sd_mpeg7_TextualType};
static const bim_attr_t t_sd_tva05_TermNameType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "preferred", .simple = &s_sd_5}
};
static const bim_type_t t_sd_tva05_TermNameType = {.attrs = t_sd_tva05_TermNameType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "TermNameType", .ns = ns_tva05, .base = &t_sd_mpeg7_TextualType};
static const char *const s_sd_35_v[] = {"albumTitle", "alternative", "episodeTitle", "main", "opusNumber", "original", "popular", "secondary", "seriesTitle", "songTitle"};
static const bim_simple_t s_sd_35 = {.codec = BIM_ENUM, .enums = s_sd_35_v, .nenums = 10};
static const bim_simple_t *const s_sd_36_m[] = {&s_sd_35, &s_sd_2};
static const bim_simple_t s_sd_36 = {.codec = BIM_UNION, .members = s_sd_36_m, .nmembers = 2};
static const bim_attr_t t_sd_mpeg7_TitleType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "type", .simple = &s_sd_36}
};
static const bim_type_t t_sd_mpeg7_TitleType = {.attrs = t_sd_mpeg7_TitleType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "TitleType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_TextualBaseType};
static const bim_attr_t t_sd_tva05_ShortTitleType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "length", .required = 1, .simple = &s_sd_4},
  {.name = "type", .simple = &s_sd_36}
};
static const bim_type_t t_sd_tva05_ShortTitleType = {.attrs = t_sd_tva05_ShortTitleType_a, .nattrs = 3, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ShortTitleType", .ns = ns_tva05, .base = &t_sd_mpeg7_TitleType};
static const char *const s_sd_37_v[] = {"base16", "base64", "text"};
static const bim_simple_t s_sd_37 = {.codec = BIM_ENUM, .enums = s_sd_37_v, .nenums = 3};
static const bim_attr_t t_sd_mpeg7_UniqueIDType_a[] = {
  {.name = "authority", .simple = &s_sd_0},
  {.name = "encoding", .simple = &s_sd_37},
  {.name = "organization", .simple = &s_sd_0},
  {.name = "type", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_UniqueIDType = {.attrs = t_sd_mpeg7_UniqueIDType_a, .nattrs = 4, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "UniqueIDType", .ns = ns_mpeg7, .base = &t_sd_xs_string};
static const bim_type_t t_sd_mpeg7_basicDurationType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "basicDurationType", .ns = ns_mpeg7, .base = &t_sd_xs_string};
static const bim_type_t t_sd_mpeg7_durationType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "durationType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_basicDurationType};
static const bim_type_t t_sd_mpeg7_mediaDurationType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "mediaDurationType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_basicDurationType};
static const bim_type_t t_sd_mpeg7_mediaTimeOffsetType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "mediaTimeOffsetType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_basicDurationType};
static const bim_type_t t_sd_mpeg7_MediaRelTimePointType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "MediaRelTimePointType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_mediaTimeOffsetType};
static const bim_type_t t_sd_mpeg7_timeOffsetType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "timeOffsetType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_basicDurationType};
static const bim_attr_t t_sd_mpeg7_RelTimePointType_a[] = {
  {.name = "timeBase", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_RelTimePointType = {.attrs = t_sd_mpeg7_RelTimePointType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "RelTimePointType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_timeOffsetType};
static const bim_type_t t_sd_mpeg7_basicTimePointType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "basicTimePointType", .ns = ns_mpeg7, .base = &t_sd_xs_string};
static const bim_type_t t_sd_mpeg7_mediaTimePointType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "mediaTimePointType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_basicTimePointType};
static const bim_type_t t_sd_mpeg7_timePointType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "timePointType", .ns = ns_mpeg7, .base = &t_sd_mpeg7_basicTimePointType};
static const bim_type_t t_sd_mpeg7_characterSetCode = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "characterSetCode", .ns = ns_mpeg7, .base = &t_sd_xs_string};
static const bim_type_t t_sd_mpeg7_countryCode = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "countryCode", .ns = ns_mpeg7, .base = &t_sd_xs_string};
static const bim_type_t t_sd_mpeg7_mimeType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "mimeType", .ns = ns_mpeg7, .base = &t_sd_xs_string};
static const bim_type_t t_sd_mpeg7_regionCode = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "regionCode", .ns = ns_mpeg7, .base = &t_sd_xs_string};
static const bim_attr_t t_sd_mpeg7_08_NameComponentType_a[] = {
  {.name = "abbrev", .simple = &s_sd_0},
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "initial", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_08_NameComponentType = {.attrs = t_sd_mpeg7_08_NameComponentType_a, .nattrs = 3, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "NameComponentType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_TextualBaseType};
static const bim_attr_t t_sd_tva11_ExplanationType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "length", .simple = &s_sd_31}
};
static const bim_type_t t_sd_tva11_ExplanationType = {.attrs = t_sd_tva11_ExplanationType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ExplanationType", .ns = ns_tva11, .base = &t_sd_mpeg7_08_TextualType};
static const bim_attr_t t_sd_tva11_KeywordType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "metadataOriginIDRef", .simple = &s_sd_0},
  {.name = "type", .simple = &s_sd_34}
};
static const bim_type_t t_sd_tva11_KeywordType = {.attrs = t_sd_tva11_KeywordType_a, .nattrs = 3, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "KeywordType", .ns = ns_tva11, .base = &t_sd_mpeg7_08_TextualType};
static const bim_attr_t t_sd_tva11_OrganizationNameType_a[] = {
  {.name = "fragmentExpirationDate", .simple = &s_sd_0},
  {.name = "fragmentId", .simple = &s_sd_0},
  {.name = "fragmentVersion", .simple = &s_sd_9},
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "metadataOriginIDRef", .simple = &s_sd_0},
  {.name = "organizationNameId", .required = 1, .simple = &s_sd_0}
};
static const bim_type_t t_sd_tva11_OrganizationNameType = {.attrs = t_sd_tva11_OrganizationNameType_a, .nattrs = 6, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "OrganizationNameType", .ns = ns_tva11, .base = &t_sd_mpeg7_08_TextualType};
static const bim_attr_t t_sd_tva11_ServiceInformationNameType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "length", .simple = &s_sd_31}
};
static const bim_type_t t_sd_tva11_ServiceInformationNameType = {.attrs = t_sd_tva11_ServiceInformationNameType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ServiceInformationNameType", .ns = ns_tva11, .base = &t_sd_mpeg7_08_TextualType};
static const bim_attr_t t_sd_tva11_SynopsisType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "length", .simple = &s_sd_31}
};
static const bim_type_t t_sd_tva11_SynopsisType = {.attrs = t_sd_tva11_SynopsisType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "SynopsisType", .ns = ns_tva11, .base = &t_sd_mpeg7_08_TextualType};
static const bim_attr_t t_sd_mpeg7_08_TitleType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "type", .simple = &s_sd_36}
};
static const bim_type_t t_sd_mpeg7_08_TitleType = {.attrs = t_sd_mpeg7_08_TitleType_a, .nattrs = 2, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "TitleType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_TextualBaseType};
static const bim_attr_t t_sd_tva11_ShortTitleType_a[] = {
  {.name = "lang", .ns = ns_xml, .simple = &s_sd_0},
  {.name = "length", .required = 1, .simple = &s_sd_4},
  {.name = "type", .simple = &s_sd_36}
};
static const bim_type_t t_sd_tva11_ShortTitleType = {.attrs = t_sd_tva11_ShortTitleType_a, .nattrs = 3, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ShortTitleType", .ns = ns_tva11, .base = &t_sd_mpeg7_08_TitleType};
static const bim_attr_t t_sd_mpeg7_08_UniqueIDType_a[] = {
  {.name = "authority", .required = 1, .simple = &s_sd_0},
  {.name = "encoding", .simple = &s_sd_37},
  {.name = "organization", .simple = &s_sd_0},
  {.name = "type", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_08_UniqueIDType = {.attrs = t_sd_mpeg7_08_UniqueIDType_a, .nattrs = 4, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "UniqueIDType", .ns = ns_mpeg7_08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_mpeg7_08_basicDurationType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "basicDurationType", .ns = ns_mpeg7_08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_mpeg7_08_durationType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "durationType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_basicDurationType};
static const bim_type_t t_sd_mpeg7_08_mediaDurationType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "mediaDurationType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_basicDurationType};
static const bim_type_t t_sd_mpeg7_08_mediaTimeOffsetType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "mediaTimeOffsetType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_basicDurationType};
static const bim_type_t t_sd_mpeg7_08_MediaRelTimePointType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "MediaRelTimePointType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_mediaTimeOffsetType};
static const bim_type_t t_sd_mpeg7_08_timeOffsetType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "timeOffsetType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_basicDurationType};
static const bim_attr_t t_sd_mpeg7_08_RelTimePointType_a[] = {
  {.name = "timeBase", .simple = &s_sd_0}
};
static const bim_type_t t_sd_mpeg7_08_RelTimePointType = {.attrs = t_sd_mpeg7_08_RelTimePointType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "RelTimePointType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_timeOffsetType};
static const bim_type_t t_sd_mpeg7_08_basicTimePointType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "basicTimePointType", .ns = ns_mpeg7_08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_mpeg7_08_mediaTimePointType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "mediaTimePointType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_basicTimePointType};
static const bim_type_t t_sd_mpeg7_08_timePointType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "timePointType", .ns = ns_mpeg7_08, .base = &t_sd_mpeg7_08_basicTimePointType};
static const bim_type_t t_sd_mpeg7_08_characterSetCode = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "characterSetCode", .ns = ns_mpeg7_08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_mpeg7_08_countryCode = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "countryCode", .ns = ns_mpeg7_08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_mpeg7_08_mimeType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "mimeType", .ns = ns_mpeg7_08, .base = &t_sd_xs_string};
static const bim_type_t t_sd_mpeg7_08_regionCode = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "regionCode", .ns = ns_mpeg7_08, .base = &t_sd_xs_string};
static const bim_attr_t t_sd_tva11_GenreType_a[] = {
  {.name = "href", .required = 1, .simple = &s_sd_2},
  {.name = "metadataOriginIDRef", .simple = &s_sd_0},
  {.name = "type", .simple = &s_sd_34}
};
static const bim_part_t t_sd_tva11_GenreType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_tva11, .type = &t_sd_tva11_TermNameType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Definition", .ns = ns_tva11, .type = &t_sd_mpeg7_08_TextualType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_tva11_GenreType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_tva11_GenreType_p, .nkids = 2};
static const bim_type_t t_sd_tva11_GenreType = {.attrs = t_sd_tva11_GenreType_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_tva11_GenreType_top, .name = "GenreType", .ns = ns_tva11, .base = &t_sd_tva11_ControlledTermType};
static const bim_attr_t t_sd_s08_DVBSTPTransportModeType_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_2},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_2}
};
static const bim_part_t t_sd_s08_DVBSTPTransportModeType_top = {.kind = BIM_PART_ELEM, .name = "PayloadId", .ns = ns_s08, .type = &t_sd_s08_PayloadList_PayloadId, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_DVBSTPTransportModeType = {.attrs = t_sd_s08_DVBSTPTransportModeType_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_DVBSTPTransportModeType_top, .name = "DVBSTPTransportModeType", .ns = ns_s08, .base = &t_sd_s08_PayloadList};
static const bim_attr_t t_sd_s08_HTTPTransportModeType_a[] = {
  {.name = "Location", .required = 1, .simple = &s_sd_0},
  {.name = "SOAP", .simple = &s_sd_1}
};
static const bim_part_t t_sd_s08_HTTPTransportModeType_top = {.kind = BIM_PART_ELEM, .name = "PayloadId", .ns = ns_s08, .type = &t_sd_s08_PayloadList_PayloadId, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_HTTPTransportModeType = {.attrs = t_sd_s08_HTTPTransportModeType_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_HTTPTransportModeType_top, .name = "HTTPTransportModeType", .ns = ns_s08, .base = &t_sd_s08_PayloadList};
static const bim_part_t t_sd_s08_TransportModeType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBSTP", .ns = ns_s08, .type = &t_sd_s08_DVBSTPTransportModeType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "HTTP", .ns = ns_s08, .type = &t_sd_s08_HTTPTransportModeType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s08_TransportModeType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = BIM_UNBOUNDED, .kids = t_sd_s08_TransportModeType_p, .nkids = 2};
static const bim_type_t t_sd_s08_TransportModeType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s08_TransportModeType_top, .name = "TransportModeType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_tva05_ControlledTermType_a[] = {
  {.name = "href", .required = 1, .simple = &s_sd_2}
};
static const bim_part_t t_sd_tva05_ControlledTermType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_tva05, .type = &t_sd_tva05_TermNameType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Definition", .ns = ns_tva05, .type = &t_sd_mpeg7_TextualType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_tva05_ControlledTermType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_tva05_ControlledTermType_p, .nkids = 2};
static const bim_type_t t_sd_tva05_ControlledTermType = {.attrs = t_sd_tva05_ControlledTermType_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_tva05_ControlledTermType_top, .name = "ControlledTermType", .ns = ns_tva05, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_CDSDownloadSessionDescriptionLocationType_DVBSTP_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_2},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_2}
};
static const bim_type_t t_sd_s08_CDSDownloadSessionDescriptionLocationType_DVBSTP = {.attrs = t_sd_s08_CDSDownloadSessionDescriptionLocationType_DVBSTP_a, .nattrs = 3, .content = BIM_CONTENT_NONE, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s08_CDSDownloadSessionDescriptionLocationType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBSTP", .ns = ns_s08, .type = &t_sd_s08_CDSDownloadSessionDescriptionLocationType_DVBSTP, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "SAP", .ns = ns_s08, .type = &t_sd_s08_CDSDownloadSessionDescriptionLocationType_DVBSTP, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s08_CDSDownloadSessionDescriptionLocationType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = BIM_UNBOUNDED, .kids = t_sd_s08_CDSDownloadSessionDescriptionLocationType_p, .nkids = 2};
static const bim_type_t t_sd_s08_CDSDownloadSessionDescriptionLocationType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s08_CDSDownloadSessionDescriptionLocationType_top, .name = "CDSDownloadSessionDescriptionLocationType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_BCGOffering_BCG_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_BCGOffering_BCG_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "TransportMode", .ns = ns_s08, .type = &t_sd_s08_TransportModeType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Logo", .ns = ns_s08, .type = &t_sd_xs_anyURI, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Type", .ns = ns_s08, .type = &t_sd_tva05_ControlledTermType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TargetProvider", .ns = ns_s08, .type = &t_sd_s08_DomainType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "BCGProviderName", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CDSDownloadSessionDescriptionLocation", .ns = ns_s08, .type = &t_sd_s08_CDSDownloadSessionDescriptionLocationType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s08_BCGOffering_BCG_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_BCGOffering_BCG_p, .nkids = 8};
static const bim_type_t t_sd_s08_BCGOffering_BCG = {.attrs = t_sd_s08_BCGOffering_BCG_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_BCGOffering_BCG_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_BCGOffering_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_BCGOffering_top = {.kind = BIM_PART_ELEM, .name = "BCG", .ns = ns_s08, .type = &t_sd_s08_BCGOffering_BCG, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_BCGOffering = {.attrs = t_sd_s08_BCGOffering_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_BCGOffering_top, .name = "BCGOffering", .ns = ns_s08, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_s08_FECLayerAddressType_a[] = {
  {.name = "Address", .simple = &s_sd_2},
  {.name = "MaxBitrate", .simple = &s_sd_6},
  {.name = "PayloadTypeNumber", .simple = &s_sd_7},
  {.name = "Port", .simple = &s_sd_4},
  {.name = "RTSPControlURL", .simple = &s_sd_0},
  {.name = "Source", .simple = &s_sd_2},
  {.name = "TransportProtocol", .simple = &s_sd_8}
};
static const bim_type_t t_sd_s08_FECLayerAddressType = {.attrs = t_sd_s08_FECLayerAddressType_a, .nattrs = 7, .content = BIM_CONTENT_NONE, .name = "FECLayerAddressType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_McastType_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_2},
  {.name = "FECMaxBlockSize", .simple = &s_sd_4},
  {.name = "FECMaxBlockTime", .simple = &s_sd_4},
  {.name = "FECOTI", .simple = &s_sd_10},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_2},
  {.name = "Streaming", .simple = &s_sd_11}
};
static const bim_part_t t_sd_s08_McastType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "FECBaseLayer", .ns = ns_s08, .type = &t_sd_s08_FECLayerAddressType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "FECEnhancementLayer", .ns = ns_s08, .type = &t_sd_s08_FECLayerAddressType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CNAME", .ns = ns_s08, .type = &t_sd_xs_string, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "ssrc", .ns = ns_s08, .type = &t_sd_xs_unsignedInt, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "RTPRetransmission", .ns = ns_s08, .type = &t_sd_s08_RETInfoType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s08_McastType_top = {.kind = BIM_PART_SEQ, .min = 0, .max = 1, .kids = t_sd_s08_McastType_p, .nkids = 5};
static const bim_type_t t_sd_s08_McastType = {.attrs = t_sd_s08_McastType_a, .nattrs = 7, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_McastType_top, .name = "McastType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s12_ServiceLocation_p_0[] = {
  {.kind = BIM_PART_ELEM, .name = "IPMulticastAddress", .ns = ns_s12, .type = &t_sd_s08_McastType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "RTSPURL", .ns = ns_s12, .type = &t_sd_s08_RTSPURLType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s12_ServiceLocation_p[] = {
  {.kind = BIM_PART_CHOICE, .min = 1, .max = BIM_UNBOUNDED, .kids = t_sd_s12_ServiceLocation_p_0, .nkids = 2},
  {.kind = BIM_PART_ELEM, .name = "BroadcastSystem", .ns = ns_s12, .type = &t_sd_s12_BroadcastSystemType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s12_ServiceLocation_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = 1, .kids = t_sd_s12_ServiceLocation_p, .nkids = 2};
static const bim_type_t t_sd_s12_ServiceLocation = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s12_ServiceLocation_top, .name = "ServiceLocation", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_MosaicDescription_LogicalCell_a[] = {
  {.name = "CellId", .required = 1, .simple = &s_sd_17},
  {.name = "EventId", .simple = &s_sd_0},
  {.name = "LinkageInfo", .required = 1, .simple = &s_sd_0},
  {.name = "PresentationInfo", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_MosaicDescription_LogicalCell_p_2[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s12, .type = &t_sd_s12_DVBTriplet, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "PackageId", .ns = ns_s12, .type = &t_sd_s08_MosaicDescription_LogicalCell_PackageId, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TextualId", .ns = ns_s12, .type = &t_sd_s08_TextualIdentifier, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s12_MosaicDescription_LogicalCell_p[] = {
  {.kind = BIM_PART_ELEM, .name = "ElementaryCell", .ns = ns_s12, .type = &t_sd_s08_MosaicDescription_LogicalCell_ElementaryCell, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "AudioLink", .ns = ns_s12, .type = &t_sd_s08_MosaicDescription_LogicalCell_AudioLink, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_CHOICE, .min = 0, .max = 1, .kids = t_sd_s12_MosaicDescription_LogicalCell_p_2, .nkids = 3}
};
static const bim_part_t t_sd_s12_MosaicDescription_LogicalCell_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s12_MosaicDescription_LogicalCell_p, .nkids = 3};
static const bim_type_t t_sd_s12_MosaicDescription_LogicalCell = {.attrs = t_sd_s12_MosaicDescription_LogicalCell_a, .nattrs = 4, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_MosaicDescription_LogicalCell_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_MosaicDescription_a[] = {
  {.name = "EntryPoint", .simple = &s_sd_5},
  {.name = "HorizontalCellsNumber", .required = 1, .simple = &s_sd_0},
  {.name = "VerticalCellsNumber", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_MosaicDescription_top = {.kind = BIM_PART_ELEM, .name = "LogicalCell", .ns = ns_s12, .type = &t_sd_s12_MosaicDescription_LogicalCell, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s12_MosaicDescription = {.attrs = t_sd_s12_MosaicDescription_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_MosaicDescription_top, .name = "MosaicDescription", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_AnnouncementSupport_Announcement_a[] = {
  {.name = "ComponentTag", .simple = &s_sd_0},
  {.name = "ReferenceType", .required = 1, .simple = &s_sd_0},
  {.name = "Type", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_AnnouncementSupport_Announcement_p[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s12, .type = &t_sd_s12_DVBTriplet, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TextualIdentifier", .ns = ns_s12, .type = &t_sd_s08_TextualIdentifier, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s12_AnnouncementSupport_Announcement_top = {.kind = BIM_PART_CHOICE, .min = 0, .max = 1, .kids = t_sd_s12_AnnouncementSupport_Announcement_p, .nkids = 2};
static const bim_type_t t_sd_s12_AnnouncementSupport_Announcement = {.attrs = t_sd_s12_AnnouncementSupport_Announcement_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_AnnouncementSupport_Announcement_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_AnnouncementSupport_a[] = {
  {.name = "SupportIndicator", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_AnnouncementSupport_top = {.kind = BIM_PART_ELEM, .name = "Announcement", .ns = ns_s12, .type = &t_sd_s12_AnnouncementSupport_Announcement, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s12_AnnouncementSupport = {.attrs = t_sd_s12_AnnouncementSupport_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_AnnouncementSupport_top, .name = "AnnouncementSupport", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_SI_a[] = {
  {.name = "PrimarySISource", .simple = &s_sd_18},
  {.name = "ServiceType", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_SI_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_s12, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s12, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServiceDescriptionLocation", .ns = ns_s12, .type = &t_sd_s12_DescriptionLocationBCG, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ContentGenre", .ns = ns_s12, .type = &t_sd_s08_Genre, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CountryAvailability", .ns = ns_s12, .type = &t_sd_s08_CountryAvailability, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ReplacementService", .ns = ns_s12, .type = &t_sd_s08_ReplacementService, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "MosaicDescription", .ns = ns_s12, .type = &t_sd_s12_MosaicDescription, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "AnnouncementSupport", .ns = ns_s12, .type = &t_sd_s12_AnnouncementSupport, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s12_SI_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s12_SI_p, .nkids = 8};
static const bim_type_t t_sd_s12_SI = {.attrs = t_sd_s12_SI_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_SI_top, .name = "SI", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s12_IPService_p[] = {
  {.kind = BIM_PART_ELEM, .name = "ServiceLocation", .ns = ns_s12, .type = &t_sd_s12_ServiceLocation, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TextualIdentifier", .ns = ns_s12, .type = &t_sd_s08_TextualIdentifier, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s12, .type = &t_sd_s12_DVBTriplet, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "MaxBitrate", .ns = ns_s12, .type = &t_sd_xs_positiveInteger, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "SI", .ns = ns_s12, .type = &t_sd_s12_SI, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "AudioAttributes", .ns = ns_s12, .type = &t_sd_tva11_AudioAttributesType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "VideoAttributes", .ns = ns_s12, .type = &t_sd_tva11_VideoAttributesType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServiceAvailability", .ns = ns_s12, .type = &t_sd_s08_ServiceAvailabilityType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Usage", .ns = ns_s12, .type = &t_sd_s12_Usage, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "LinkedService", .ns = ns_s12, .type = &t_sd_s12_IPService, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s12_IPService_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s12_IPService_p, .nkids = 10};
static const bim_type_t t_sd_s12_IPService = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s12_IPService_top, .name = "IPService", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s12_IPServiceList_p[] = {
  {.kind = BIM_PART_ELEM, .name = "ServicesDescriptionLocation", .ns = ns_s12, .type = &t_sd_s12_DescriptionLocationBCG, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "SingleService", .ns = ns_s12, .type = &t_sd_s12_IPService, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s12_IPServiceList_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s12_IPServiceList_p, .nkids = 2};
static const bim_type_t t_sd_s12_IPServiceList = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s12_IPServiceList_top, .name = "IPServiceList", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_BroadcastOffering_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_BroadcastOffering_top = {.kind = BIM_PART_ELEM, .name = "ServiceList", .ns = ns_s12, .type = &t_sd_s12_IPServiceList, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s12_BroadcastOffering = {.attrs = t_sd_s12_BroadcastOffering_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_BroadcastOffering_top, .name = "BroadcastOffering", .ns = ns_s12, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_s12_PackagedServices_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_PackagedServices_top = {.kind = BIM_PART_ELEM, .name = "Package", .ns = ns_s12, .type = &t_sd_s12_Package, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s12_PackagedServices = {.attrs = t_sd_s12_PackagedServices_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_PackagedServices_top, .name = "PackagedServices", .ns = ns_s12, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_s08_RMSFUSDiscoveryType_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_RMSFUSDiscoveryType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "FUSProvider", .ns = ns_s08, .type = &t_sd_s08_FUSType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "RMSProvider", .ns = ns_s08, .type = &t_sd_s08_RMSType, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s08_RMSFUSDiscoveryType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = 1, .kids = t_sd_s08_RMSFUSDiscoveryType_p, .nkids = 2};
static const bim_type_t t_sd_s08_RMSFUSDiscoveryType = {.attrs = t_sd_s08_RMSFUSDiscoveryType_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_RMSFUSDiscoveryType_top, .name = "RMSFUSDiscoveryType", .ns = ns_s08, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_s12_SRMAnnouncementModeSAPType_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_2},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_2}
};
static const bim_type_t t_sd_s12_SRMAnnouncementModeSAPType = {.attrs = t_sd_s12_SRMAnnouncementModeSAPType_a, .nattrs = 3, .content = BIM_CONTENT_NONE, .name = "SRMAnnouncementModeSAPType", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s12_SRMAnnouncementModeType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "HTTP", .ns = ns_s12, .type = &t_sd_xs_anyURI, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "SAP", .ns = ns_s12, .type = &t_sd_s12_SRMAnnouncementModeSAPType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s12_SRMAnnouncementModeType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = 1, .kids = t_sd_s12_SRMAnnouncementModeType_p, .nkids = 2};
static const bim_type_t t_sd_s12_SRMAnnouncementModeType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s12_SRMAnnouncementModeType_top, .name = "SRMAnnouncementModeType", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_SRMAnnouncementServiceType_a[] = {
  {.name = "AnnouncementServiceVersion", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_SRMAnnouncementServiceType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "SRMID", .ns = ns_s12, .type = &t_sd_s12_SRMIDType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "SRMAnnouncementMode", .ns = ns_s12, .type = &t_sd_s12_SRMAnnouncementModeType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s12_SRMAnnouncementServiceType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s12_SRMAnnouncementServiceType_p, .nkids = 2};
static const bim_type_t t_sd_s12_SRMAnnouncementServiceType = {.attrs = t_sd_s12_SRMAnnouncementServiceType_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_SRMAnnouncementServiceType_top, .name = "SRMAnnouncementServiceType", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_SRMDownloadServiceFLUTEType_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_2},
  {.name = "FLUTESessionVersion", .simple = &s_sd_0},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_2},
  {.name = "TSI", .required = 1, .simple = &s_sd_4}
};
static const bim_part_t t_sd_s12_SRMDownloadServiceFLUTEType_top = {.kind = BIM_PART_ELEM, .name = "SRMIDVer", .ns = ns_s12, .type = &t_sd_s12_SRMIDVerMType, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s12_SRMDownloadServiceFLUTEType = {.attrs = t_sd_s12_SRMDownloadServiceFLUTEType_a, .nattrs = 5, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_SRMDownloadServiceFLUTEType_top, .name = "SRMDownloadServiceFLUTEType", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s12_SRMDownloadServiceType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "FLUTE", .ns = ns_s12, .type = &t_sd_s12_SRMDownloadServiceFLUTEType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "HTTP", .ns = ns_s12, .type = &t_sd_s12_SRMDownloadServiceHTTPType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s12_SRMDownloadServiceType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = 1, .kids = t_sd_s12_SRMDownloadServiceType_p, .nkids = 2};
static const bim_type_t t_sd_s12_SRMDownloadServiceType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s12_SRMDownloadServiceType_top, .name = "SRMDownloadServiceType", .ns = ns_s12, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s12_SRMOffering_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s12_SRMOffering_p[] = {
  {.kind = BIM_PART_ELEM, .name = "SRMAnnouncementService", .ns = ns_s12, .type = &t_sd_s12_SRMAnnouncementServiceType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "SRMDownloadService", .ns = ns_s12, .type = &t_sd_s12_SRMDownloadServiceType, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s12_SRMOffering_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s12_SRMOffering_p, .nkids = 2};
static const bim_type_t t_sd_s12_SRMOffering = {.attrs = t_sd_s12_SRMOffering_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s12_SRMOffering_top, .name = "SRMOffering", .ns = ns_s12, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_s08_OfferingListType_Pull_a[] = {
  {.name = "Location", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_OfferingListType_Pull_top = {.kind = BIM_PART_ELEM, .name = "PayloadId", .ns = ns_s08, .type = &t_sd_s08_PayloadList_PayloadId, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_OfferingListType_Pull = {.attrs = t_sd_s08_OfferingListType_Pull_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_OfferingListType_Pull_top, .base = &t_sd_s08_PayloadList};
static const bim_part_t t_sd_s08_OfferingListType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Pull", .ns = ns_s08, .type = &t_sd_s08_OfferingListType_Pull, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Push", .ns = ns_s08, .type = &t_sd_s08_DVBSTPTransportModeType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s08_OfferingListType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = BIM_UNBOUNDED, .kids = t_sd_s08_OfferingListType_p, .nkids = 2};
static const bim_type_t t_sd_s08_OfferingListType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s08_OfferingListType_top, .name = "OfferingListType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_ServiceProviderType_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "LogoURI", .simple = &s_sd_0},
  {.name = "Version", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_ServiceProviderType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Offering", .ns = ns_s08, .type = &t_sd_s08_OfferingListType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s08_ServiceProviderType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_ServiceProviderType_p, .nkids = 3};
static const bim_type_t t_sd_s08_ServiceProviderType = {.attrs = t_sd_s08_ServiceProviderType_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_ServiceProviderType_top, .name = "ServiceProviderType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s08_ServiceProviderListType_top = {.kind = BIM_PART_ELEM, .name = "ServiceProvider", .ns = ns_s08, .type = &t_sd_s08_ServiceProviderType, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_ServiceProviderListType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s08_ServiceProviderListType_top, .name = "ServiceProviderListType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_ServiceDiscovery_2_a[] = {
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_ServiceDiscovery_2_p[] = {
  {.kind = BIM_PART_ELEM, .name = "BCGDiscovery", .ns = ns_s12, .type = &t_sd_s08_BCGOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "BroadcastDiscovery", .ns = ns_s12, .type = &t_sd_s12_BroadcastOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CoDDiscovery", .ns = ns_s12, .type = &t_sd_s08_CoDOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageDiscovery", .ns = ns_s12, .type = &t_sd_s12_PackagedServices, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "RMSFUSDiscovery", .ns = ns_s12, .type = &t_sd_s08_RMSFUSDiscoveryType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "RegionalizationDiscovery", .ns = ns_s12, .type = &t_sd_s08_RegionalizationOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "SRMDiscovery", .ns = ns_s12, .type = &t_sd_s12_SRMOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServiceProviderDiscovery", .ns = ns_s12, .type = &t_sd_s08_ServiceProviderListType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServicesFromOtherSP", .ns = ns_s12, .type = &t_sd_s12_ReferencedServices, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_ServiceDiscovery_2_top = {.kind = BIM_PART_CHOICE, .min = 0, .max = 1, .kids = t_sd_ServiceDiscovery_2_p, .nkids = 9};
static const bim_type_t t_sd_ServiceDiscovery_2 = {.attrs = t_sd_ServiceDiscovery_2_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_ServiceDiscovery_2_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_tva05_GenreType_a[] = {
  {.name = "href", .required = 1, .simple = &s_sd_2},
  {.name = "metadataOriginIDRef", .simple = &s_sd_0},
  {.name = "type", .simple = &s_sd_34}
};
static const bim_part_t t_sd_tva05_GenreType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_tva05, .type = &t_sd_tva05_TermNameType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Definition", .ns = ns_tva05, .type = &t_sd_mpeg7_TextualType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_tva05_GenreType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_tva05_GenreType_p, .nkids = 2};
static const bim_type_t t_sd_tva05_GenreType = {.attrs = t_sd_tva05_GenreType_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_tva05_GenreType_top, .name = "GenreType", .ns = ns_tva05, .base = &t_sd_tva05_ControlledTermType};
static const bim_part_t t_sd_s08_ServiceLocation_p[] = {
  {.kind = BIM_PART_ELEM, .name = "IPMulticastAddress", .ns = ns_s08, .type = &t_sd_s08_McastType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "RTSPURL", .ns = ns_s08, .type = &t_sd_s08_RTSPURLType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s08_ServiceLocation_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = BIM_UNBOUNDED, .kids = t_sd_s08_ServiceLocation_p, .nkids = 2};
static const bim_type_t t_sd_s08_ServiceLocation = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s08_ServiceLocation_top, .name = "ServiceLocation", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_tva05_AudioAttributesType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Coding", .ns = ns_tva05, .type = &t_sd_tva05_ControlledTermType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "NumOfChannels", .ns = ns_tva05, .type = &t_sd_xs_unsignedShort, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "MixType", .ns = ns_tva05, .type = &t_sd_tva05_ControlledTermType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "AudioLanguage", .ns = ns_tva05, .type = &t_sd_tva05_AudioLanguageType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_tva05_AudioAttributesType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_tva05_AudioAttributesType_p, .nkids = 4};
static const bim_type_t t_sd_tva05_AudioAttributesType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_tva05_AudioAttributesType_top, .name = "AudioAttributesType", .ns = ns_tva05, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_tva05_ColorType_a[] = {
  {.name = "type", .required = 1, .simple = &s_sd_22}
};
static const bim_type_t t_sd_tva05_ColorType = {.attrs = t_sd_tva05_ColorType_a, .nattrs = 1, .content = BIM_CONTENT_NONE, .name = "ColorType", .ns = ns_tva05, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_tva05_VideoAttributesType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Coding", .ns = ns_tva05, .type = &t_sd_tva05_ControlledTermType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Scan", .ns = ns_tva05, .type = &t_sd_tva05_ScanType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "HorizontalSize", .ns = ns_tva05, .type = &t_sd_xs_unsignedShort, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "VerticalSize", .ns = ns_tva05, .type = &t_sd_xs_unsignedShort, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "AspectRatio", .ns = ns_tva05, .type = &t_sd_tva05_AspectRatioType, .min = 0, .max = 2},
  {.kind = BIM_PART_ELEM, .name = "Color", .ns = ns_tva05, .type = &t_sd_tva05_ColorType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "FrameRate", .ns = ns_tva05, .type = &t_sd_tva05_FrameRateType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_tva05_VideoAttributesType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_tva05_VideoAttributesType_p, .nkids = 7};
static const bim_type_t t_sd_tva05_VideoAttributesType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_tva05_VideoAttributesType_top, .name = "VideoAttributesType", .ns = ns_tva05, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s08_IPService_p[] = {
  {.kind = BIM_PART_ELEM, .name = "ServiceLocation", .ns = ns_s08, .type = &t_sd_s08_ServiceLocation, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TextualIdentifier", .ns = ns_s08, .type = &t_sd_s08_TextualIdentifier, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s08, .type = &t_sd_s08_DVBTriplet, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "MaxBitrate", .ns = ns_s08, .type = &t_sd_xs_positiveInteger, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "SI", .ns = ns_s08, .type = &t_sd_s08_SI, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "AudioAttributes", .ns = ns_s08, .type = &t_sd_tva05_AudioAttributesType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "VideoAttributes", .ns = ns_s08, .type = &t_sd_tva05_VideoAttributesType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServiceAvailability", .ns = ns_s08, .type = &t_sd_s08_ServiceAvailabilityType, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s08_IPService_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_IPService_p, .nkids = 8};
static const bim_type_t t_sd_s08_IPService = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s08_IPService_top, .name = "IPService", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s08_IPServiceList_p[] = {
  {.kind = BIM_PART_ELEM, .name = "ServicesDescriptionLocation", .ns = ns_s08, .type = &t_sd_s08_DescriptionLocationBCG, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "SingleService", .ns = ns_s08, .type = &t_sd_s08_IPService, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s08_IPServiceList_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_IPServiceList_p, .nkids = 2};
static const bim_type_t t_sd_s08_IPServiceList = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s08_IPServiceList_top, .name = "IPServiceList", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_BroadcastOffering_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_BroadcastOffering_top = {.kind = BIM_PART_ELEM, .name = "ServiceList", .ns = ns_s08, .type = &t_sd_s08_IPServiceList, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_BroadcastOffering = {.attrs = t_sd_s08_BroadcastOffering_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_BroadcastOffering_top, .name = "BroadcastOffering", .ns = ns_s08, .base = &t_sd_s08_OfferingBase};
static const bim_part_t t_sd_s08_PackagedServiceType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "TextualID", .ns = ns_s08, .type = &t_sd_s08_TextualIdentifier, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s08, .type = &t_sd_s08_DVBTriplet, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "DescriptionLocation", .ns = ns_s08, .type = &t_sd_s08_DescriptionLocationBCG, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "LogicalChannelNumber", .ns = ns_s08, .type = &t_sd_xs_positiveInteger, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s08_PackagedServiceType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_PackagedServiceType_p, .nkids = 4};
static const bim_type_t t_sd_s08_PackagedServiceType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s08_PackagedServiceType_top, .name = "PackagedServiceType", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_Package_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0},
  {.name = "Visible", .simple = &s_sd_5}
};
static const bim_part_t t_sd_s08_Package_p[] = {
  {.kind = BIM_PART_ELEM, .name = "PackageName", .ns = ns_s08, .type = &t_sd_s08_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CountryAvailability", .ns = ns_s08, .type = &t_sd_s08_CountryAvailability, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageDescription", .ns = ns_s08, .type = &t_sd_s08_DescriptionLocationBCG, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Service", .ns = ns_s08, .type = &t_sd_s08_PackagedServiceType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageReference", .ns = ns_s08, .type = &t_sd_s12_Package_PackageReference, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageAvailability", .ns = ns_s08, .type = &t_sd_s08_ServiceAvailabilityType, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s08_Package_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s08_Package_p, .nkids = 6};
static const bim_type_t t_sd_s08_Package = {.attrs = t_sd_s08_Package_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_Package_top, .name = "Package", .ns = ns_s08, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_PackagedServices_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_PackagedServices_top = {.kind = BIM_PART_ELEM, .name = "Package", .ns = ns_s08, .type = &t_sd_s08_Package, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_PackagedServices = {.attrs = t_sd_s08_PackagedServices_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_PackagedServices_top, .name = "PackagedServices", .ns = ns_s08, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_s08_ReferencedServices_ReferencedServiceProvider_a[] = {
  {.name = "Domain", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_ReferencedServices_ReferencedServiceProvider_top = {.kind = BIM_PART_ELEM, .name = "Service", .ns = ns_s08, .type = &t_sd_s12_ReferencedServiceProviderType_Service, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_ReferencedServices_ReferencedServiceProvider = {.attrs = t_sd_s08_ReferencedServices_ReferencedServiceProvider_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_ReferencedServices_ReferencedServiceProvider_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s08_ReferencedServices_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s08_ReferencedServices_top = {.kind = BIM_PART_ELEM, .name = "ReferencedServiceProvider", .ns = ns_s08, .type = &t_sd_s08_ReferencedServices_ReferencedServiceProvider, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s08_ReferencedServices = {.attrs = t_sd_s08_ReferencedServices_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s08_ReferencedServices_top, .name = "ReferencedServices", .ns = ns_s08, .base = &t_sd_s08_OfferingBase};
static const bim_attr_t t_sd_ServiceDiscovery_3_a[] = {
  {.name = "Version", .simple = &s_sd_0}
};
static const bim_part_t t_sd_ServiceDiscovery_3_p[] = {
  {.kind = BIM_PART_ELEM, .name = "BCGDiscovery", .ns = ns_s08, .type = &t_sd_s08_BCGOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "BroadcastDiscovery", .ns = ns_s08, .type = &t_sd_s08_BroadcastOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CoDDiscovery", .ns = ns_s08, .type = &t_sd_s08_CoDOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageDiscovery", .ns = ns_s08, .type = &t_sd_s08_PackagedServices, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "RMSFUSDiscovery", .ns = ns_s08, .type = &t_sd_s08_RMSFUSDiscoveryType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "RegionalizationDiscovery", .ns = ns_s08, .type = &t_sd_s08_RegionalizationOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServiceProviderDiscovery", .ns = ns_s08, .type = &t_sd_s08_ServiceProviderListType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServicesFromOtherSP", .ns = ns_s08, .type = &t_sd_s08_ReferencedServices, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_ServiceDiscovery_3_top = {.kind = BIM_PART_CHOICE, .min = 0, .max = 1, .kids = t_sd_ServiceDiscovery_3_p, .nkids = 8};
static const bim_type_t t_sd_ServiceDiscovery_3 = {.attrs = t_sd_ServiceDiscovery_3_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_ServiceDiscovery_3_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_OfferingBase_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_25}
};
static const bim_type_t t_sd_s06_OfferingBase = {.attrs = t_sd_s06_OfferingBase_a, .nattrs = 2, .content = BIM_CONTENT_NONE, .name = "OfferingBase", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_MultilingualType_a[] = {
  {.name = "Language", .required = 1, .simple = &s_sd_0}
};
static const bim_type_t t_sd_s06_MultilingualType = {.attrs = t_sd_s06_MultilingualType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "MultilingualType", .ns = ns_s06, .base = &t_sd_xs_string};
static const bim_attr_t t_sd_s06_PayloadList_PayloadId_Segment_a[] = {
  {.name = "ID", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_25}
};
static const bim_type_t t_sd_s06_PayloadList_PayloadId_Segment = {.attrs = t_sd_s06_PayloadList_PayloadId_Segment_a, .nattrs = 2, .content = BIM_CONTENT_NONE, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_PayloadList_PayloadId_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s06_PayloadList_PayloadId_top = {.kind = BIM_PART_ELEM, .name = "Segment", .ns = ns_s06, .type = &t_sd_s06_PayloadList_PayloadId_Segment, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_PayloadList_PayloadId = {.attrs = t_sd_s06_PayloadList_PayloadId_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_PayloadList_PayloadId_top, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s06_PayloadList_top = {.kind = BIM_PART_ELEM, .name = "PayloadId", .ns = ns_s06, .type = &t_sd_s06_PayloadList_PayloadId, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_PayloadList = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s06_PayloadList_top, .name = "PayloadList", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_DVBSTPTransportModeType_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_2},
  {.name = "FECMaxBlockSize", .simple = &s_sd_4},
  {.name = "FECMaxBlockTime", .simple = &s_sd_4},
  {.name = "FECOTI", .simple = &s_sd_10},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_2},
  {.name = "Streaming", .simple = &s_sd_11}
};
static const bim_part_t t_sd_s06_DVBSTPTransportModeType_top = {.kind = BIM_PART_ELEM, .name = "PayloadId", .ns = ns_s06, .type = &t_sd_s06_PayloadList_PayloadId, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_DVBSTPTransportModeType = {.attrs = t_sd_s06_DVBSTPTransportModeType_a, .nattrs = 7, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_DVBSTPTransportModeType_top, .name = "DVBSTPTransportModeType", .ns = ns_s06, .base = &t_sd_s06_PayloadList};
static const bim_attr_t t_sd_s06_HTTPTransportModeType_a[] = {
  {.name = "Location", .required = 1, .simple = &s_sd_0},
  {.name = "SOAP", .simple = &s_sd_1}
};
static const bim_part_t t_sd_s06_HTTPTransportModeType_top = {.kind = BIM_PART_ELEM, .name = "PayloadId", .ns = ns_s06, .type = &t_sd_s06_PayloadList_PayloadId, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_HTTPTransportModeType = {.attrs = t_sd_s06_HTTPTransportModeType_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_HTTPTransportModeType_top, .name = "HTTPTransportModeType", .ns = ns_s06, .base = &t_sd_s06_PayloadList};
static const bim_part_t t_sd_s06_TransportModeType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBSTP", .ns = ns_s06, .type = &t_sd_s06_DVBSTPTransportModeType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "HTTP", .ns = ns_s06, .type = &t_sd_s06_HTTPTransportModeType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s06_TransportModeType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = BIM_UNBOUNDED, .kids = t_sd_s06_TransportModeType_p, .nkids = 2};
static const bim_type_t t_sd_s06_TransportModeType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s06_TransportModeType_top, .name = "TransportModeType", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_s06_DomainType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "DomainType", .ns = ns_s06, .base = &t_sd_xs_string};
static const bim_attr_t t_sd_s06_BCGOffering_BCG_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_25}
};
static const bim_part_t t_sd_s06_BCGOffering_BCG_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_s06, .type = &t_sd_s06_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s06, .type = &t_sd_s06_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "TransportMode", .ns = ns_s06, .type = &t_sd_s06_TransportModeType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Logo", .ns = ns_s06, .type = &t_sd_xs_anyURI, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Type", .ns = ns_s06, .type = &t_sd_tva05_ControlledTermType, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TargetProvider", .ns = ns_s06, .type = &t_sd_s06_DomainType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "BCGProviderName", .ns = ns_s06, .type = &t_sd_s06_MultilingualType, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s06_BCGOffering_BCG_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s06_BCGOffering_BCG_p, .nkids = 7};
static const bim_type_t t_sd_s06_BCGOffering_BCG = {.attrs = t_sd_s06_BCGOffering_BCG_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_BCGOffering_BCG_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_BCGOffering_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_25}
};
static const bim_part_t t_sd_s06_BCGOffering_top = {.kind = BIM_PART_ELEM, .name = "BCG", .ns = ns_s06, .type = &t_sd_s06_BCGOffering_BCG, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_BCGOffering = {.attrs = t_sd_s06_BCGOffering_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_BCGOffering_top, .name = "BCGOffering", .ns = ns_s06, .base = &t_sd_s06_OfferingBase};
static const bim_type_t t_sd_s06_DescriptionLocation = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "DescriptionLocation", .ns = ns_s06, .base = &t_sd_xs_anyURI};
static const bim_attr_t t_sd_s06_DescriptionLocationBCG_a[] = {
  {.name = "preferred", .simple = &s_sd_5}
};
static const bim_type_t t_sd_s06_DescriptionLocationBCG = {.attrs = t_sd_s06_DescriptionLocationBCG_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "DescriptionLocationBCG", .ns = ns_s06, .base = &t_sd_s06_DescriptionLocation};
static const bim_attr_t t_sd_s06_FECLayerAddressType_a[] = {
  {.name = "Address", .simple = &s_sd_2},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_2}
};
static const bim_type_t t_sd_s06_FECLayerAddressType = {.attrs = t_sd_s06_FECLayerAddressType_a, .nattrs = 3, .content = BIM_CONTENT_NONE, .name = "FECLayerAddressType", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_McastType_a[] = {
  {.name = "Address", .required = 1, .simple = &s_sd_2},
  {.name = "FECMaxBlockSize", .simple = &s_sd_4},
  {.name = "FECMaxBlockTime", .simple = &s_sd_4},
  {.name = "FECOTI", .simple = &s_sd_10},
  {.name = "Port", .required = 1, .simple = &s_sd_4},
  {.name = "Source", .simple = &s_sd_2},
  {.name = "Streaming", .simple = &s_sd_11}
};
static const bim_part_t t_sd_s06_McastType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "FECBaseLayer", .ns = ns_s06, .type = &t_sd_s06_FECLayerAddressType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "FECEnhancementLayer", .ns = ns_s06, .type = &t_sd_s06_FECLayerAddressType, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s06_McastType_top = {.kind = BIM_PART_SEQ, .min = 0, .max = 1, .kids = t_sd_s06_McastType_p, .nkids = 2};
static const bim_type_t t_sd_s06_McastType = {.attrs = t_sd_s06_McastType_a, .nattrs = 7, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_McastType_top, .name = "McastType", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_s06_RTSP = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "RTSP", .ns = ns_s06, .base = &t_sd_xs_anyURI};
static const bim_part_t t_sd_s06_ServiceLocation_p[] = {
  {.kind = BIM_PART_ELEM, .name = "IPMulticastAddress", .ns = ns_s06, .type = &t_sd_s06_McastType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "RTSPURL", .ns = ns_s06, .type = &t_sd_s06_RTSP, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s06_ServiceLocation_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = BIM_UNBOUNDED, .kids = t_sd_s06_ServiceLocation_p, .nkids = 2};
static const bim_type_t t_sd_s06_ServiceLocation = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s06_ServiceLocation_top, .name = "ServiceLocation", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_TextualIdentifier_a[] = {
  {.name = "DomainName", .simple = &s_sd_0},
  {.name = "ServiceName", .required = 1, .simple = &s_sd_0}
};
static const bim_type_t t_sd_s06_TextualIdentifier = {.attrs = t_sd_s06_TextualIdentifier_a, .nattrs = 2, .content = BIM_CONTENT_NONE, .name = "TextualIdentifier", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_DVBTriplet_a[] = {
  {.name = "OrigNetId", .required = 1, .simple = &s_sd_4},
  {.name = "ServiceId", .required = 1, .simple = &s_sd_4},
  {.name = "TSId", .required = 1, .simple = &s_sd_4}
};
static const bim_type_t t_sd_s06_DVBTriplet = {.attrs = t_sd_s06_DVBTriplet_a, .nattrs = 3, .content = BIM_CONTENT_NONE, .name = "DVBTriplet", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_s06_Genre = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_16, .name = "Genre", .ns = ns_s06, .base = &t_sd_xs_byte};
static const bim_attr_t t_sd_s06_CountryAvailability_a[] = {
  {.name = "Available", .simple = &s_sd_5},
  {.name = "Countries", .required = 1, .simple = &s_sd_0}
};
static const bim_type_t t_sd_s06_CountryAvailability = {.attrs = t_sd_s06_CountryAvailability_a, .nattrs = 2, .content = BIM_CONTENT_NONE, .name = "CountryAvailability", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_ReplacementService_a[] = {
  {.name = "ReplacementType", .simple = &s_sd_0}
};
static const bim_part_t t_sd_s06_ReplacementService_p[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s06, .type = &t_sd_s06_DVBTriplet, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TextualIdentifier", .ns = ns_s06, .type = &t_sd_s06_TextualIdentifier, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s06_ReplacementService_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = 1, .kids = t_sd_s06_ReplacementService_p, .nkids = 2};
static const bim_type_t t_sd_s06_ReplacementService = {.attrs = t_sd_s06_ReplacementService_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_ReplacementService_top, .name = "ReplacementService", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_s06_Hexadecimal16bit = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Hexadecimal16bit", .ns = ns_s06, .base = &t_sd_xs_string};
static const bim_attr_t t_sd_s06_MosaicDescription_LogicalCell_PackageId_a[] = {
  {.name = "Domain", .simple = &s_sd_0}
};
static const bim_type_t t_sd_s06_MosaicDescription_LogicalCell_PackageId = {.attrs = t_sd_s06_MosaicDescription_LogicalCell_PackageId_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .base = &t_sd_s06_Hexadecimal16bit};
static const bim_attr_t t_sd_s06_MosaicDescription_LogicalCell_a[] = {
  {.name = "CellId", .required = 1, .simple = &s_sd_17},
  {.name = "EventId", .simple = &s_sd_0},
  {.name = "LinkageInfo", .required = 1, .simple = &s_sd_0},
  {.name = "PresentationInfo", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s06_MosaicDescription_LogicalCell_p_2[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s06, .type = &t_sd_s06_DVBTriplet, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "PackageId", .ns = ns_s06, .type = &t_sd_s06_MosaicDescription_LogicalCell_PackageId, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TextualId", .ns = ns_s06, .type = &t_sd_s06_TextualIdentifier, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s06_MosaicDescription_LogicalCell_p[] = {
  {.kind = BIM_PART_ELEM, .name = "ElementaryCell", .ns = ns_s06, .type = &t_sd_s08_MosaicDescription_LogicalCell_ElementaryCell, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "AudioLink", .ns = ns_s06, .type = &t_sd_s08_MosaicDescription_LogicalCell_AudioLink, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_CHOICE, .min = 0, .max = 1, .kids = t_sd_s06_MosaicDescription_LogicalCell_p_2, .nkids = 3}
};
static const bim_part_t t_sd_s06_MosaicDescription_LogicalCell_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s06_MosaicDescription_LogicalCell_p, .nkids = 3};
static const bim_type_t t_sd_s06_MosaicDescription_LogicalCell = {.attrs = t_sd_s06_MosaicDescription_LogicalCell_a, .nattrs = 4, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_MosaicDescription_LogicalCell_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_MosaicDescription_a[] = {
  {.name = "EntryPoint", .simple = &s_sd_5},
  {.name = "HorizontalCellsNumber", .required = 1, .simple = &s_sd_0},
  {.name = "VerticalCellsNumber", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s06_MosaicDescription_top = {.kind = BIM_PART_ELEM, .name = "LogicalCell", .ns = ns_s06, .type = &t_sd_s06_MosaicDescription_LogicalCell, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_MosaicDescription = {.attrs = t_sd_s06_MosaicDescription_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_MosaicDescription_top, .name = "MosaicDescription", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_AnnouncementSupport_Announcement_a[] = {
  {.name = "ComponentTag", .simple = &s_sd_0},
  {.name = "ReferenceType", .required = 1, .simple = &s_sd_0},
  {.name = "Type", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s06_AnnouncementSupport_Announcement_p[] = {
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s06, .type = &t_sd_s06_DVBTriplet, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TextualIdentifier", .ns = ns_s06, .type = &t_sd_s06_TextualIdentifier, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s06_AnnouncementSupport_Announcement_top = {.kind = BIM_PART_CHOICE, .min = 0, .max = 1, .kids = t_sd_s06_AnnouncementSupport_Announcement_p, .nkids = 2};
static const bim_type_t t_sd_s06_AnnouncementSupport_Announcement = {.attrs = t_sd_s06_AnnouncementSupport_Announcement_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_AnnouncementSupport_Announcement_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_AnnouncementSupport_a[] = {
  {.name = "SupportIndicator", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s06_AnnouncementSupport_top = {.kind = BIM_PART_ELEM, .name = "Announcement", .ns = ns_s06, .type = &t_sd_s06_AnnouncementSupport_Announcement, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_AnnouncementSupport = {.attrs = t_sd_s06_AnnouncementSupport_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_AnnouncementSupport_top, .name = "AnnouncementSupport", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_SI_a[] = {
  {.name = "PrimarySISource", .simple = &s_sd_18},
  {.name = "ServiceType", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s06_SI_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_s06, .type = &t_sd_s06_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s06, .type = &t_sd_s06_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServiceDescriptionLocation", .ns = ns_s06, .type = &t_sd_s06_DescriptionLocationBCG, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ContentGenre", .ns = ns_s06, .type = &t_sd_s06_Genre, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CountryAvailability", .ns = ns_s06, .type = &t_sd_s06_CountryAvailability, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ReplacementService", .ns = ns_s06, .type = &t_sd_s06_ReplacementService, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "MosaicDescription", .ns = ns_s06, .type = &t_sd_s06_MosaicDescription, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "AnnouncementSupport", .ns = ns_s06, .type = &t_sd_s06_AnnouncementSupport, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s06_SI_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s06_SI_p, .nkids = 8};
static const bim_type_t t_sd_s06_SI = {.attrs = t_sd_s06_SI_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_SI_top, .name = "SI", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_PackageAvailabilityCountryCodeType_a[] = {
  {.name = "Availability", .simple = &s_sd_5}
};
static const bim_type_t t_sd_s06_PackageAvailabilityCountryCodeType = {.attrs = t_sd_s06_PackageAvailabilityCountryCodeType_a, .nattrs = 1, .content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "PackageAvailabilityCountryCodeType", .ns = ns_s06, .base = &t_sd_xs_string};
static const bim_part_t t_sd_s06_ServiceAvailabilityType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "CountryCode", .ns = ns_s06, .type = &t_sd_s06_PackageAvailabilityCountryCodeType, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Cells", .ns = ns_s06, .type = &t_sd_xs_string, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s06_ServiceAvailabilityType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s06_ServiceAvailabilityType_p, .nkids = 2};
static const bim_type_t t_sd_s06_ServiceAvailabilityType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s06_ServiceAvailabilityType_top, .name = "ServiceAvailabilityType", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s06_IPService_p[] = {
  {.kind = BIM_PART_ELEM, .name = "ServiceLocation", .ns = ns_s06, .type = &t_sd_s06_ServiceLocation, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "TextualIdentifier", .ns = ns_s06, .type = &t_sd_s06_TextualIdentifier, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s06, .type = &t_sd_s06_DVBTriplet, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "MaxBitrate", .ns = ns_s06, .type = &t_sd_xs_positiveInteger, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "SI", .ns = ns_s06, .type = &t_sd_s06_SI, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "AudioAttributes", .ns = ns_s06, .type = &t_sd_tva05_AudioAttributesType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "VideoAttributes", .ns = ns_s06, .type = &t_sd_tva05_VideoAttributesType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServiceAvailability", .ns = ns_s06, .type = &t_sd_s06_ServiceAvailabilityType, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s06_IPService_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s06_IPService_p, .nkids = 8};
static const bim_type_t t_sd_s06_IPService = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s06_IPService_top, .name = "IPService", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s06_IPServiceList_p[] = {
  {.kind = BIM_PART_ELEM, .name = "ServicesDescriptionLocation", .ns = ns_s06, .type = &t_sd_s06_DescriptionLocationBCG, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "SingleService", .ns = ns_s06, .type = &t_sd_s06_IPService, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s06_IPServiceList_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s06_IPServiceList_p, .nkids = 2};
static const bim_type_t t_sd_s06_IPServiceList = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s06_IPServiceList_top, .name = "IPServiceList", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_BroadcastOffering_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_25}
};
static const bim_part_t t_sd_s06_BroadcastOffering_top = {.kind = BIM_PART_ELEM, .name = "ServiceList", .ns = ns_s06, .type = &t_sd_s06_IPServiceList, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_BroadcastOffering = {.attrs = t_sd_s06_BroadcastOffering_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_BroadcastOffering_top, .name = "BroadcastOffering", .ns = ns_s06, .base = &t_sd_s06_OfferingBase};
static const bim_attr_t t_sd_s06_CoDOffering_Catalogue_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s06_CoDOffering_Catalogue_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_s06, .type = &t_sd_s06_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s06, .type = &t_sd_s06_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Locator", .ns = ns_s06, .type = &t_sd_xs_anyURI, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s06_CoDOffering_Catalogue_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s06_CoDOffering_Catalogue_p, .nkids = 3};
static const bim_type_t t_sd_s06_CoDOffering_Catalogue = {.attrs = t_sd_s06_CoDOffering_Catalogue_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_CoDOffering_Catalogue_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_CoDOffering_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_25}
};
static const bim_part_t t_sd_s06_CoDOffering_top = {.kind = BIM_PART_ELEM, .name = "Catalogue", .ns = ns_s06, .type = &t_sd_s06_CoDOffering_Catalogue, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_CoDOffering = {.attrs = t_sd_s06_CoDOffering_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_CoDOffering_top, .name = "CoDOffering", .ns = ns_s06, .base = &t_sd_s06_OfferingBase};
static const bim_part_t t_sd_s06_PackagedServiceType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "TextualID", .ns = ns_s06, .type = &t_sd_s06_TextualIdentifier, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "DVBTriplet", .ns = ns_s06, .type = &t_sd_s06_DVBTriplet, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "DescriptionLocation", .ns = ns_s06, .type = &t_sd_s06_DescriptionLocationBCG, .min = 0, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "LogicalChannelNumber", .ns = ns_s06, .type = &t_sd_xs_positiveInteger, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s06_PackagedServiceType_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s06_PackagedServiceType_p, .nkids = 4};
static const bim_type_t t_sd_s06_PackagedServiceType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s06_PackagedServiceType_top, .name = "PackagedServiceType", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_Package_a[] = {
  {.name = "Id", .required = 1, .simple = &s_sd_0},
  {.name = "Visible", .simple = &s_sd_5}
};
static const bim_part_t t_sd_s06_Package_p[] = {
  {.kind = BIM_PART_ELEM, .name = "PackageName", .ns = ns_s06, .type = &t_sd_s06_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CountryAvailability", .ns = ns_s06, .type = &t_sd_s06_CountryAvailability, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageDescription", .ns = ns_s06, .type = &t_sd_s06_DescriptionLocationBCG, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Service", .ns = ns_s06, .type = &t_sd_s06_PackagedServiceType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageReference", .ns = ns_s06, .type = &t_sd_s12_Package_PackageReference, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageAvailability", .ns = ns_s06, .type = &t_sd_s06_ServiceAvailabilityType, .min = 0, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_s06_Package_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s06_Package_p, .nkids = 6};
static const bim_type_t t_sd_s06_Package = {.attrs = t_sd_s06_Package_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_Package_top, .name = "Package", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_PackagedServices_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_25}
};
static const bim_part_t t_sd_s06_PackagedServices_top = {.kind = BIM_PART_ELEM, .name = "Package", .ns = ns_s06, .type = &t_sd_s06_Package, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_PackagedServices = {.attrs = t_sd_s06_PackagedServices_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_PackagedServices_top, .name = "PackagedServices", .ns = ns_s06, .base = &t_sd_s06_OfferingBase};
static const bim_attr_t t_sd_s06_OfferingListType_Pull_a[] = {
  {.name = "Location", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s06_OfferingListType_Pull_top = {.kind = BIM_PART_ELEM, .name = "PayloadId", .ns = ns_s06, .type = &t_sd_s06_PayloadList_PayloadId, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_OfferingListType_Pull = {.attrs = t_sd_s06_OfferingListType_Pull_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_OfferingListType_Pull_top, .base = &t_sd_s06_PayloadList};
static const bim_part_t t_sd_s06_OfferingListType_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Pull", .ns = ns_s06, .type = &t_sd_s06_OfferingListType_Pull, .min = 1, .max = 1},
  {.kind = BIM_PART_ELEM, .name = "Push", .ns = ns_s06, .type = &t_sd_s06_DVBSTPTransportModeType, .min = 1, .max = 1}
};
static const bim_part_t t_sd_s06_OfferingListType_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = BIM_UNBOUNDED, .kids = t_sd_s06_OfferingListType_p, .nkids = 2};
static const bim_type_t t_sd_s06_OfferingListType = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s06_OfferingListType_top, .name = "OfferingListType", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_ServiceProvider_ServiceProvider_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "LogoURI", .simple = &s_sd_0},
  {.name = "Version", .required = 1, .simple = &s_sd_25}
};
static const bim_part_t t_sd_s06_ServiceProvider_ServiceProvider_p[] = {
  {.kind = BIM_PART_ELEM, .name = "Name", .ns = ns_s06, .type = &t_sd_s06_MultilingualType, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Description", .ns = ns_s06, .type = &t_sd_s06_MultilingualType, .min = 0, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "Offering", .ns = ns_s06, .type = &t_sd_s06_OfferingListType, .min = 0, .max = 1}
};
static const bim_part_t t_sd_s06_ServiceProvider_ServiceProvider_top = {.kind = BIM_PART_SEQ, .min = 1, .max = 1, .kids = t_sd_s06_ServiceProvider_ServiceProvider_p, .nkids = 3};
static const bim_type_t t_sd_s06_ServiceProvider_ServiceProvider = {.attrs = t_sd_s06_ServiceProvider_ServiceProvider_a, .nattrs = 3, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_ServiceProvider_ServiceProvider_top, .base = &t_sd_xs_anyType};
static const bim_part_t t_sd_s06_ServiceProvider_top = {.kind = BIM_PART_ELEM, .name = "ServiceProvider", .ns = ns_s06, .type = &t_sd_s06_ServiceProvider_ServiceProvider, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_ServiceProvider = {.content = BIM_CONTENT_SEQ, .top = &t_sd_s06_ServiceProvider_top, .name = "ServiceProvider", .ns = ns_s06, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_ReferencedServices_ReferencedServiceProvider_a[] = {
  {.name = "Domain", .required = 1, .simple = &s_sd_0}
};
static const bim_part_t t_sd_s06_ReferencedServices_ReferencedServiceProvider_top = {.kind = BIM_PART_ELEM, .name = "Service", .ns = ns_s06, .type = &t_sd_s12_ReferencedServiceProviderType_Service, .min = 0, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_ReferencedServices_ReferencedServiceProvider = {.attrs = t_sd_s06_ReferencedServices_ReferencedServiceProvider_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_ReferencedServices_ReferencedServiceProvider_top, .base = &t_sd_xs_anyType};
static const bim_attr_t t_sd_s06_ReferencedServices_a[] = {
  {.name = "DomainName", .required = 1, .simple = &s_sd_0},
  {.name = "Version", .simple = &s_sd_25}
};
static const bim_part_t t_sd_s06_ReferencedServices_top = {.kind = BIM_PART_ELEM, .name = "ReferencedServiceProvider", .ns = ns_s06, .type = &t_sd_s06_ReferencedServices_ReferencedServiceProvider, .min = 1, .max = BIM_UNBOUNDED};
static const bim_type_t t_sd_s06_ReferencedServices = {.attrs = t_sd_s06_ReferencedServices_a, .nattrs = 2, .content = BIM_CONTENT_SEQ, .top = &t_sd_s06_ReferencedServices_top, .name = "ReferencedServices", .ns = ns_s06, .base = &t_sd_s06_OfferingBase};
static const bim_attr_t t_sd_ServiceDiscovery_4_a[] = {
  {.name = "Version", .simple = &s_sd_25}
};
static const bim_part_t t_sd_ServiceDiscovery_4_p[] = {
  {.kind = BIM_PART_ELEM, .name = "BCGDiscovery", .ns = ns_s06, .type = &t_sd_s06_BCGOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "BroadcastDiscovery", .ns = ns_s06, .type = &t_sd_s06_BroadcastOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "CoDDiscovery", .ns = ns_s06, .type = &t_sd_s06_CoDOffering, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "PackageDiscovery", .ns = ns_s06, .type = &t_sd_s06_PackagedServices, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServiceProviderDiscovery", .ns = ns_s06, .type = &t_sd_s06_ServiceProvider, .min = 1, .max = BIM_UNBOUNDED},
  {.kind = BIM_PART_ELEM, .name = "ServicesFromOtherSP", .ns = ns_s06, .type = &t_sd_s06_ReferencedServices, .min = 1, .max = BIM_UNBOUNDED}
};
static const bim_part_t t_sd_ServiceDiscovery_4_top = {.kind = BIM_PART_CHOICE, .min = 1, .max = 1, .kids = t_sd_ServiceDiscovery_4_p, .nkids = 6};
static const bim_type_t t_sd_ServiceDiscovery_4 = {.attrs = t_sd_ServiceDiscovery_4_a, .nattrs = 1, .content = BIM_CONTENT_SEQ, .top = &t_sd_ServiceDiscovery_4_top, .base = &t_sd_xs_anyType};
static const bim_type_t t_sd_s06_PullURL = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "PullURL", .ns = ns_s06, .base = &t_sd_xs_anyURI};
static const bim_type_t t_sd_s06_Integer6bit = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_17, .name = "Integer6bit", .ns = ns_s06, .base = &t_sd_xs_unsignedShort};
static const bim_type_t t_sd_s06_OrigNetId = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_4, .name = "OrigNetId", .ns = ns_s06, .base = &t_sd_xs_unsignedShort};
static const bim_type_t t_sd_s06_ServiceId = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_4, .name = "ServiceId", .ns = ns_s06, .base = &t_sd_xs_unsignedShort};
static const bim_type_t t_sd_s06_TSId = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_4, .name = "TSId", .ns = ns_s06, .base = &t_sd_xs_unsignedShort};
static const bim_type_t t_sd_s06_Hexadecimal3bit = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Hexadecimal3bit", .ns = ns_s06, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s06_Hexadecimal4bit = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Hexadecimal4bit", .ns = ns_s06, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s06_Hexadecimal8bit = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Hexadecimal8bit", .ns = ns_s06, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s06_ServiceType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ServiceType", .ns = ns_s06, .base = &t_sd_s06_Hexadecimal8bit};
static const bim_type_t t_sd_s06_IPType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "IPType", .ns = ns_s06, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s06_ISO_3166_List = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ISO-3166-List", .ns = ns_s06, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s06_ISO639_2 = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "ISO639-2", .ns = ns_s06, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s06_PrimarySISource = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_18, .name = "PrimarySISource", .ns = ns_s06, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s06_Service = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_0, .name = "Service", .ns = ns_s06, .base = &t_sd_xs_string};
static const bim_type_t t_sd_s06_StreamingType = {.content = BIM_CONTENT_SIMPLE, .simple = &s_sd_11, .name = "StreamingType", .ns = ns_s06, .base = &t_sd_xs_string};

static const bim_type_t *const sd_subs_2014_1_0[] = {&t_sd_s08_DescriptionLocation, &t_sd_s08_DescriptionLocationBCG, &t_sd_s08_PullURL, &t_sd_s08_RTSP, &t_sd_s08_RTSPURLType, &t_sd_tva05_CRIDType, &t_sd_tva05_InstanceMetadataIdType, &t_sd_tva11_CRIDType, &t_sd_tva11_InstanceMetadataIdType};
static const bim_type_t *const sd_subs_2014_1_1[] = {&t_sd_s08_DescriptionLocationBCG};
static const bim_type_t *const sd_subs_2014_1_2[] = {&t_sd_s08_RTSPURLType};
static const bim_type_t *const sd_subs_2014_1_3[] = {&t_sd_xs_integer, &t_sd_xs_long, &t_sd_xs_int, &t_sd_xs_short, &t_sd_xs_byte, &t_sd_s08_Genre, &t_sd_xs_nonNegativeInteger, &t_sd_xs_positiveInteger, &t_sd_xs_unsignedLong, &t_sd_xs_unsignedInt, &t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId, &t_sd_tva05_BitRateType, &t_sd_tva11_BitRateType, &t_sd_mpeg7_unsigned12, &t_sd_mpeg7_unsigned8, &t_sd_mpeg7_08_unsigned12, &t_sd_mpeg7_08_unsigned8, &t_sd_xs_nonPositiveInteger, &t_sd_xs_negativeInteger, &t_sd_mpeg7_IncrDurationType, &t_sd_mpeg7_MediaIncrDurationType, &t_sd_mpeg7_MediaRelIncrTimePointType, &t_sd_tva05_TVAMediaRelIncrTimePointType, &t_sd_mpeg7_RelIncrTimePointType, &t_sd_mpeg7_preferenceValueType, &t_sd_mpeg7_08_IncrDurationType, &t_sd_mpeg7_08_MediaIncrDurationType, &t_sd_mpeg7_08_MediaRelIncrTimePointType, &t_sd_tva11_TVAMediaRelIncrTimePointType, &t_sd_mpeg7_08_RelIncrTimePointType, &t_sd_mpeg7_08_preferenceValueType};
static const bim_type_t *const sd_subs_2014_1_4[] = {&t_sd_xs_long, &t_sd_xs_int, &t_sd_xs_short, &t_sd_xs_byte, &t_sd_s08_Genre, &t_sd_xs_nonNegativeInteger, &t_sd_xs_positiveInteger, &t_sd_xs_unsignedLong, &t_sd_xs_unsignedInt, &t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId, &t_sd_tva05_BitRateType, &t_sd_tva11_BitRateType, &t_sd_mpeg7_unsigned12, &t_sd_mpeg7_unsigned8, &t_sd_mpeg7_08_unsigned12, &t_sd_mpeg7_08_unsigned8, &t_sd_xs_nonPositiveInteger, &t_sd_xs_negativeInteger, &t_sd_mpeg7_IncrDurationType, &t_sd_mpeg7_MediaIncrDurationType, &t_sd_mpeg7_MediaRelIncrTimePointType, &t_sd_tva05_TVAMediaRelIncrTimePointType, &t_sd_mpeg7_RelIncrTimePointType, &t_sd_mpeg7_preferenceValueType, &t_sd_mpeg7_08_IncrDurationType, &t_sd_mpeg7_08_MediaIncrDurationType, &t_sd_mpeg7_08_MediaRelIncrTimePointType, &t_sd_tva11_TVAMediaRelIncrTimePointType, &t_sd_mpeg7_08_RelIncrTimePointType, &t_sd_mpeg7_08_preferenceValueType};
static const bim_type_t *const sd_subs_2014_1_5[] = {&t_sd_xs_int, &t_sd_xs_short, &t_sd_xs_byte, &t_sd_s08_Genre};
static const bim_type_t *const sd_subs_2014_1_6[] = {&t_sd_xs_short, &t_sd_xs_byte, &t_sd_s08_Genre};
static const bim_type_t *const sd_subs_2014_1_7[] = {&t_sd_xs_byte, &t_sd_s08_Genre};
static const bim_type_t *const sd_subs_2014_1_8[] = {&t_sd_s08_Genre};
static const bim_type_t *const sd_subs_2014_1_9[] = {&t_sd_xs_positiveInteger, &t_sd_xs_unsignedLong, &t_sd_xs_unsignedInt, &t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId, &t_sd_tva05_BitRateType, &t_sd_tva11_BitRateType, &t_sd_mpeg7_unsigned12, &t_sd_mpeg7_unsigned8, &t_sd_mpeg7_08_unsigned12, &t_sd_mpeg7_08_unsigned8};
static const bim_type_t *const sd_subs_2014_1_10[] = {&t_sd_xs_unsignedInt, &t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId};
static const bim_type_t *const sd_subs_2014_1_11[] = {&t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId};
static const bim_type_t *const sd_subs_2014_1_12[] = {&t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId};
static const bim_type_t *const sd_subs_2014_1_13[] = {&t_sd_xs_negativeInteger};
static const bim_type_t *const sd_subs_2014_1_14[] = {&t_sd_tva05_TVAMediaRelIncrTimePointType};
static const bim_type_t *const sd_subs_2014_1_15[] = {&t_sd_tva11_TVAMediaRelIncrTimePointType};
static const bim_type_t *const sd_subs_2014_1_16[] = {&t_sd_xs_normalizedString, &t_sd_xs_token, &t_sd_xs_NMTOKEN, &t_sd_mpeg7_userChoiceType, &t_sd_mpeg7_08_userChoiceType, &t_sd_xs_Name, &t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF, &t_sd_xs_language, &t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_tva11_CaptionLanguageType, &t_sd_tva11_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_08_ExtendedLanguageType, &t_sd_tva11_AudioLanguageType, &t_sd_mpeg7_xPathType, &t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType, &t_sd_mpeg7_08_xPathType, &t_sd_mpeg7_08_xPathRefType, &t_sd_mpeg7_08_xPathSelectorType, &t_sd_mpeg7_08_xPathAbsoluteSelectorType, &t_sd_s08_DomainType, &t_sd_s08_Hexadecimal16bit, &t_sd_s08_Hexadecimal32bit, &t_sd_s08_Hexadecimal3bit, &t_sd_s08_Hexadecimal4bit, &t_sd_s08_Hexadecimal8bit, &t_sd_s08_ServiceType, &t_sd_s08_IPType, &t_sd_s08_ISO_3166_List, &t_sd_s08_ISO639_2, &t_sd_s08_MultilingualType, &t_sd_s08_PackageAvailabilityCountryCodeType, &t_sd_s08_PrimarySISource, &t_sd_s08_Service, &t_sd_s08_StreamingType, &t_sd_s08_TransportProtocolType, &t_sd_s08_Version, &t_sd_s12_BroadcastSystemType, &t_sd_s12_CPSystemIDType, &t_sd_s12_CPSystemSRMIDType, &t_sd_s12_EnhancementServiceType, &t_sd_s12_Usage, &t_sd_s14_IPv4Type, &t_sd_s14_IPv6Type, &t_sd_s14_Usage, &t_sd_tva05_ColorTypeType, &t_sd_tva05_FrameRateType, &t_sd_tva05_RatioType, &t_sd_tva05_AspectRatioType, &t_sd_tva05_ScanType, &t_sd_tva05_SynopsisLengthType, &t_sd_tva05_TVAIDRefType, &t_sd_tva05_TVAIDType, &t_sd_tva05_currencyCodeType, &t_sd_tva05_segmentTypeType, &t_sd_tva05_serviceInformationNameLengthType, &t_sd_tva11_ColorTypeType, &t_sd_tva11_DeliveryModeType, &t_sd_tva11_ExplanationLengthType, &t_sd_tva11_FrameRateType, &t_sd_tva11_RatioType, &t_sd_tva11_AspectRatioType, &t_sd_tva11_ScanType, &t_sd_tva11_SynopsisLengthType, &t_sd_tva11_TVAIDRefType, &t_sd_tva11_TVAIDType, &t_sd_s12_DescriptionLocationBCG, &t_sd_tva11_currencyCodeType, &t_sd_tva11_segmentTypeType, &t_sd_tva11_serviceInformationNameLengthType, &t_sd_mpeg7_TextualBaseType, &t_sd_mpeg7_NameComponentType, &t_sd_mpeg7_TextualType, &t_sd_tva05_KeywordType, &t_sd_tva05_OrganizationNameType, &t_sd_tva05_ServiceInformationNameType, &t_sd_tva05_SynopsisType, &t_sd_tva05_TermNameType, &t_sd_mpeg7_TitleType, &t_sd_tva05_ShortTitleType, &t_sd_mpeg7_UniqueIDType, &t_sd_mpeg7_basicDurationType, &t_sd_mpeg7_durationType, &t_sd_mpeg7_mediaDurationType, &t_sd_mpeg7_mediaTimeOffsetType, &t_sd_mpeg7_MediaRelTimePointType, &t_sd_mpeg7_timeOffsetType, &t_sd_mpeg7_RelTimePointType, &t_sd_mpeg7_basicTimePointType, &t_sd_mpeg7_mediaTimePointType, &t_sd_mpeg7_timePointType, &t_sd_mpeg7_characterSetCode, &t_sd_mpeg7_countryCode, &t_sd_mpeg7_mimeType, &t_sd_mpeg7_regionCode, &t_sd_mpeg7_08_TextualBaseType, &t_sd_mpeg7_08_NameComponentType, &t_sd_mpeg7_08_TextualType, &t_sd_tva11_ExplanationType, &t_sd_tva11_KeywordType, &t_sd_tva11_OrganizationNameType, &t_sd_tva11_ServiceInformationNameType, &t_sd_tva11_SynopsisType, &t_sd_tva11_TermNameType, &t_sd_mpeg7_08_TitleType, &t_sd_tva11_ShortTitleType, &t_sd_mpeg7_08_UniqueIDType, &t_sd_mpeg7_08_basicDurationType, &t_sd_mpeg7_08_durationType, &t_sd_mpeg7_08_mediaDurationType, &t_sd_mpeg7_08_mediaTimeOffsetType, &t_sd_mpeg7_08_MediaRelTimePointType, &t_sd_mpeg7_08_timeOffsetType, &t_sd_mpeg7_08_RelTimePointType, &t_sd_mpeg7_08_basicTimePointType, &t_sd_mpeg7_08_mediaTimePointType, &t_sd_mpeg7_08_timePointType, &t_sd_mpeg7_08_characterSetCode, &t_sd_mpeg7_08_countryCode, &t_sd_mpeg7_08_mimeType, &t_sd_mpeg7_08_regionCode};
static const bim_type_t *const sd_subs_2014_1_17[] = {&t_sd_xs_token, &t_sd_xs_NMTOKEN, &t_sd_mpeg7_userChoiceType, &t_sd_mpeg7_08_userChoiceType, &t_sd_xs_Name, &t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF, &t_sd_xs_language, &t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_tva11_CaptionLanguageType, &t_sd_tva11_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_08_ExtendedLanguageType, &t_sd_tva11_AudioLanguageType, &t_sd_mpeg7_xPathType, &t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType, &t_sd_mpeg7_08_xPathType, &t_sd_mpeg7_08_xPathRefType, &t_sd_mpeg7_08_xPathSelectorType, &t_sd_mpeg7_08_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2014_1_18[] = {&t_sd_xs_NMTOKEN, &t_sd_mpeg7_userChoiceType, &t_sd_mpeg7_08_userChoiceType, &t_sd_xs_Name, &t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF, &t_sd_xs_language, &t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_tva11_CaptionLanguageType, &t_sd_tva11_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_08_ExtendedLanguageType, &t_sd_tva11_AudioLanguageType, &t_sd_mpeg7_xPathType, &t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType, &t_sd_mpeg7_08_xPathType, &t_sd_mpeg7_08_xPathRefType, &t_sd_mpeg7_08_xPathSelectorType, &t_sd_mpeg7_08_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2014_1_19[] = {&t_sd_mpeg7_userChoiceType, &t_sd_mpeg7_08_userChoiceType};
static const bim_type_t *const sd_subs_2014_1_20[] = {&t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF};
static const bim_type_t *const sd_subs_2014_1_21[] = {&t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF};
static const bim_type_t *const sd_subs_2014_1_22[] = {&t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_tva11_CaptionLanguageType, &t_sd_tva11_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_08_ExtendedLanguageType, &t_sd_tva11_AudioLanguageType};
static const bim_type_t *const sd_subs_2014_1_23[] = {&t_sd_tva05_AudioLanguageType};
static const bim_type_t *const sd_subs_2014_1_24[] = {&t_sd_tva11_AudioLanguageType};
static const bim_type_t *const sd_subs_2014_1_25[] = {&t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2014_1_26[] = {&t_sd_mpeg7_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2014_1_27[] = {&t_sd_mpeg7_08_xPathRefType, &t_sd_mpeg7_08_xPathSelectorType, &t_sd_mpeg7_08_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2014_1_28[] = {&t_sd_mpeg7_08_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2014_1_29[] = {&t_sd_s08_ServiceType};
static const bim_type_t *const sd_subs_2014_1_30[] = {&t_sd_tva05_AspectRatioType};
static const bim_type_t *const sd_subs_2014_1_31[] = {&t_sd_tva11_AspectRatioType};
static const bim_type_t *const sd_subs_2014_1_32[] = {&t_sd_s12_DescriptionLocationBCG};
static const bim_type_t *const sd_subs_2014_1_33[] = {&t_sd_mpeg7_NameComponentType, &t_sd_mpeg7_TextualType, &t_sd_tva05_KeywordType, &t_sd_tva05_OrganizationNameType, &t_sd_tva05_ServiceInformationNameType, &t_sd_tva05_SynopsisType, &t_sd_tva05_TermNameType, &t_sd_mpeg7_TitleType, &t_sd_tva05_ShortTitleType};
static const bim_type_t *const sd_subs_2014_1_34[] = {&t_sd_tva05_KeywordType, &t_sd_tva05_OrganizationNameType, &t_sd_tva05_ServiceInformationNameType, &t_sd_tva05_SynopsisType, &t_sd_tva05_TermNameType};
static const bim_type_t *const sd_subs_2014_1_35[] = {&t_sd_tva05_ShortTitleType};
static const bim_type_t *const sd_subs_2014_1_36[] = {&t_sd_mpeg7_durationType, &t_sd_mpeg7_mediaDurationType, &t_sd_mpeg7_mediaTimeOffsetType, &t_sd_mpeg7_MediaRelTimePointType, &t_sd_mpeg7_timeOffsetType, &t_sd_mpeg7_RelTimePointType};
static const bim_type_t *const sd_subs_2014_1_37[] = {&t_sd_mpeg7_MediaRelTimePointType};
static const bim_type_t *const sd_subs_2014_1_38[] = {&t_sd_mpeg7_RelTimePointType};
static const bim_type_t *const sd_subs_2014_1_39[] = {&t_sd_mpeg7_mediaTimePointType, &t_sd_mpeg7_timePointType};
static const bim_type_t *const sd_subs_2014_1_40[] = {&t_sd_mpeg7_08_NameComponentType, &t_sd_mpeg7_08_TextualType, &t_sd_tva11_ExplanationType, &t_sd_tva11_KeywordType, &t_sd_tva11_OrganizationNameType, &t_sd_tva11_ServiceInformationNameType, &t_sd_tva11_SynopsisType, &t_sd_tva11_TermNameType, &t_sd_mpeg7_08_TitleType, &t_sd_tva11_ShortTitleType};
static const bim_type_t *const sd_subs_2014_1_41[] = {&t_sd_tva11_ExplanationType, &t_sd_tva11_KeywordType, &t_sd_tva11_OrganizationNameType, &t_sd_tva11_ServiceInformationNameType, &t_sd_tva11_SynopsisType, &t_sd_tva11_TermNameType};
static const bim_type_t *const sd_subs_2014_1_42[] = {&t_sd_tva11_ShortTitleType};
static const bim_type_t *const sd_subs_2014_1_43[] = {&t_sd_mpeg7_08_durationType, &t_sd_mpeg7_08_mediaDurationType, &t_sd_mpeg7_08_mediaTimeOffsetType, &t_sd_mpeg7_08_MediaRelTimePointType, &t_sd_mpeg7_08_timeOffsetType, &t_sd_mpeg7_08_RelTimePointType};
static const bim_type_t *const sd_subs_2014_1_44[] = {&t_sd_mpeg7_08_MediaRelTimePointType};
static const bim_type_t *const sd_subs_2014_1_45[] = {&t_sd_mpeg7_08_RelTimePointType};
static const bim_type_t *const sd_subs_2014_1_46[] = {&t_sd_mpeg7_08_mediaTimePointType, &t_sd_mpeg7_08_timePointType};
static const bim_type_t *const sd_subs_2014_1_47[] = {&t_sd_s12_PackageTextualIdentifier};
static const bim_type_t *const sd_subs_2014_1_48[] = {&t_sd_s12_SRMIDVerMType, &t_sd_s12_SRMIDVerUType};
static const bim_type_t *const sd_subs_2014_1_49[] = {&t_sd_tva11_GenreType};
static const bim_subtypes_t sd_subtypes_2014_1[] = {
  {&t_sd_xs_anyURI, sd_subs_2014_1_0, 9, NULL},
  {&t_sd_s08_DescriptionLocation, sd_subs_2014_1_1, 1, NULL},
  {&t_sd_s08_RTSP, sd_subs_2014_1_2, 1, NULL},
  {&t_sd_xs_decimal, sd_subs_2014_1_3, 36, NULL},
  {&t_sd_xs_integer, sd_subs_2014_1_4, 35, NULL},
  {&t_sd_xs_long, sd_subs_2014_1_5, 4, NULL},
  {&t_sd_xs_int, sd_subs_2014_1_6, 3, NULL},
  {&t_sd_xs_short, sd_subs_2014_1_7, 2, NULL},
  {&t_sd_xs_byte, sd_subs_2014_1_8, 1, NULL},
  {&t_sd_xs_nonNegativeInteger, sd_subs_2014_1_9, 15, NULL},
  {&t_sd_xs_unsignedLong, sd_subs_2014_1_10, 7, NULL},
  {&t_sd_xs_unsignedInt, sd_subs_2014_1_11, 6, NULL},
  {&t_sd_xs_unsignedShort, sd_subs_2014_1_12, 5, NULL},
  {&t_sd_xs_nonPositiveInteger, sd_subs_2014_1_13, 1, NULL},
  {&t_sd_mpeg7_MediaRelIncrTimePointType, sd_subs_2014_1_14, 1, NULL},
  {&t_sd_mpeg7_08_MediaRelIncrTimePointType, sd_subs_2014_1_15, 1, NULL},
  {&t_sd_xs_string, sd_subs_2014_1_16, 128, NULL},
  {&t_sd_xs_normalizedString, sd_subs_2014_1_17, 26, NULL},
  {&t_sd_xs_token, sd_subs_2014_1_18, 25, NULL},
  {&t_sd_xs_NMTOKEN, sd_subs_2014_1_19, 2, NULL},
  {&t_sd_xs_Name, sd_subs_2014_1_20, 4, NULL},
  {&t_sd_xs_NCName, sd_subs_2014_1_21, 3, NULL},
  {&t_sd_xs_language, sd_subs_2014_1_22, 8, NULL},
  {&t_sd_mpeg7_ExtendedLanguageType, sd_subs_2014_1_23, 1, NULL},
  {&t_sd_mpeg7_08_ExtendedLanguageType, sd_subs_2014_1_24, 1, NULL},
  {&t_sd_mpeg7_xPathType, sd_subs_2014_1_25, 3, NULL},
  {&t_sd_mpeg7_xPathSelectorType, sd_subs_2014_1_26, 1, NULL},
  {&t_sd_mpeg7_08_xPathType, sd_subs_2014_1_27, 3, NULL},
  {&t_sd_mpeg7_08_xPathSelectorType, sd_subs_2014_1_28, 1, NULL},
  {&t_sd_s08_Hexadecimal8bit, sd_subs_2014_1_29, 1, NULL},
  {&t_sd_tva05_RatioType, sd_subs_2014_1_30, 1, NULL},
  {&t_sd_tva11_RatioType, sd_subs_2014_1_31, 1, NULL},
  {&t_sd_tva11_TVAIDType, sd_subs_2014_1_32, 1, NULL},
  {&t_sd_mpeg7_TextualBaseType, sd_subs_2014_1_33, 9, NULL},
  {&t_sd_mpeg7_TextualType, sd_subs_2014_1_34, 5, NULL},
  {&t_sd_mpeg7_TitleType, sd_subs_2014_1_35, 1, NULL},
  {&t_sd_mpeg7_basicDurationType, sd_subs_2014_1_36, 6, NULL},
  {&t_sd_mpeg7_mediaTimeOffsetType, sd_subs_2014_1_37, 1, NULL},
  {&t_sd_mpeg7_timeOffsetType, sd_subs_2014_1_38, 1, NULL},
  {&t_sd_mpeg7_basicTimePointType, sd_subs_2014_1_39, 2, NULL},
  {&t_sd_mpeg7_08_TextualBaseType, sd_subs_2014_1_40, 10, NULL},
  {&t_sd_mpeg7_08_TextualType, sd_subs_2014_1_41, 6, NULL},
  {&t_sd_mpeg7_08_TitleType, sd_subs_2014_1_42, 1, NULL},
  {&t_sd_mpeg7_08_basicDurationType, sd_subs_2014_1_43, 6, NULL},
  {&t_sd_mpeg7_08_mediaTimeOffsetType, sd_subs_2014_1_44, 1, NULL},
  {&t_sd_mpeg7_08_timeOffsetType, sd_subs_2014_1_45, 1, NULL},
  {&t_sd_mpeg7_08_basicTimePointType, sd_subs_2014_1_46, 2, NULL},
  {&t_sd_s08_TextualIdentifier, sd_subs_2014_1_47, 1, NULL},
  {&t_sd_s12_SRMIDType, sd_subs_2014_1_48, 2, NULL},
  {&t_sd_tva11_ControlledTermType, sd_subs_2014_1_49, 1, NULL}
};
static const sdns_prefix_t sd_prefixes_2014_1[] = {{"s08", ns_s08}, {"s12", ns_s12}, {"tva11", ns_tva11}};
static const bim_type_t *const sd_subs_2012_1_0[] = {&t_sd_s08_DescriptionLocation, &t_sd_s08_DescriptionLocationBCG, &t_sd_s08_PullURL, &t_sd_s08_RTSP, &t_sd_s08_RTSPURLType, &t_sd_tva05_CRIDType, &t_sd_tva05_InstanceMetadataIdType, &t_sd_tva11_CRIDType, &t_sd_tva11_InstanceMetadataIdType};
static const bim_type_t *const sd_subs_2012_1_1[] = {&t_sd_s08_DescriptionLocationBCG};
static const bim_type_t *const sd_subs_2012_1_2[] = {&t_sd_s08_RTSPURLType};
static const bim_type_t *const sd_subs_2012_1_3[] = {&t_sd_xs_integer, &t_sd_xs_long, &t_sd_xs_int, &t_sd_xs_short, &t_sd_xs_byte, &t_sd_s08_Genre, &t_sd_xs_nonNegativeInteger, &t_sd_xs_positiveInteger, &t_sd_xs_unsignedLong, &t_sd_xs_unsignedInt, &t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId, &t_sd_tva05_BitRateType, &t_sd_tva11_BitRateType, &t_sd_mpeg7_unsigned12, &t_sd_mpeg7_unsigned8, &t_sd_mpeg7_08_unsigned12, &t_sd_mpeg7_08_unsigned8, &t_sd_xs_nonPositiveInteger, &t_sd_xs_negativeInteger, &t_sd_mpeg7_IncrDurationType, &t_sd_mpeg7_MediaIncrDurationType, &t_sd_mpeg7_MediaRelIncrTimePointType, &t_sd_tva05_TVAMediaRelIncrTimePointType, &t_sd_mpeg7_RelIncrTimePointType, &t_sd_mpeg7_preferenceValueType, &t_sd_mpeg7_08_IncrDurationType, &t_sd_mpeg7_08_MediaIncrDurationType, &t_sd_mpeg7_08_MediaRelIncrTimePointType, &t_sd_tva11_TVAMediaRelIncrTimePointType, &t_sd_mpeg7_08_RelIncrTimePointType, &t_sd_mpeg7_08_preferenceValueType};
static const bim_type_t *const sd_subs_2012_1_4[] = {&t_sd_xs_long, &t_sd_xs_int, &t_sd_xs_short, &t_sd_xs_byte, &t_sd_s08_Genre, &t_sd_xs_nonNegativeInteger, &t_sd_xs_positiveInteger, &t_sd_xs_unsignedLong, &t_sd_xs_unsignedInt, &t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId, &t_sd_tva05_BitRateType, &t_sd_tva11_BitRateType, &t_sd_mpeg7_unsigned12, &t_sd_mpeg7_unsigned8, &t_sd_mpeg7_08_unsigned12, &t_sd_mpeg7_08_unsigned8, &t_sd_xs_nonPositiveInteger, &t_sd_xs_negativeInteger, &t_sd_mpeg7_IncrDurationType, &t_sd_mpeg7_MediaIncrDurationType, &t_sd_mpeg7_MediaRelIncrTimePointType, &t_sd_tva05_TVAMediaRelIncrTimePointType, &t_sd_mpeg7_RelIncrTimePointType, &t_sd_mpeg7_preferenceValueType, &t_sd_mpeg7_08_IncrDurationType, &t_sd_mpeg7_08_MediaIncrDurationType, &t_sd_mpeg7_08_MediaRelIncrTimePointType, &t_sd_tva11_TVAMediaRelIncrTimePointType, &t_sd_mpeg7_08_RelIncrTimePointType, &t_sd_mpeg7_08_preferenceValueType};
static const bim_type_t *const sd_subs_2012_1_5[] = {&t_sd_xs_int, &t_sd_xs_short, &t_sd_xs_byte, &t_sd_s08_Genre};
static const bim_type_t *const sd_subs_2012_1_6[] = {&t_sd_xs_short, &t_sd_xs_byte, &t_sd_s08_Genre};
static const bim_type_t *const sd_subs_2012_1_7[] = {&t_sd_xs_byte, &t_sd_s08_Genre};
static const bim_type_t *const sd_subs_2012_1_8[] = {&t_sd_s08_Genre};
static const bim_type_t *const sd_subs_2012_1_9[] = {&t_sd_xs_positiveInteger, &t_sd_xs_unsignedLong, &t_sd_xs_unsignedInt, &t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId, &t_sd_tva05_BitRateType, &t_sd_tva11_BitRateType, &t_sd_mpeg7_unsigned12, &t_sd_mpeg7_unsigned8, &t_sd_mpeg7_08_unsigned12, &t_sd_mpeg7_08_unsigned8};
static const bim_type_t *const sd_subs_2012_1_10[] = {&t_sd_xs_unsignedInt, &t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId};
static const bim_type_t *const sd_subs_2012_1_11[] = {&t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId};
static const bim_type_t *const sd_subs_2012_1_12[] = {&t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId};
static const bim_type_t *const sd_subs_2012_1_13[] = {&t_sd_xs_negativeInteger};
static const bim_type_t *const sd_subs_2012_1_14[] = {&t_sd_tva05_TVAMediaRelIncrTimePointType};
static const bim_type_t *const sd_subs_2012_1_15[] = {&t_sd_tva11_TVAMediaRelIncrTimePointType};
static const bim_type_t *const sd_subs_2012_1_16[] = {&t_sd_xs_normalizedString, &t_sd_xs_token, &t_sd_xs_NMTOKEN, &t_sd_mpeg7_userChoiceType, &t_sd_mpeg7_08_userChoiceType, &t_sd_xs_Name, &t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF, &t_sd_xs_language, &t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_tva11_CaptionLanguageType, &t_sd_tva11_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_08_ExtendedLanguageType, &t_sd_tva11_AudioLanguageType, &t_sd_mpeg7_xPathType, &t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType, &t_sd_mpeg7_08_xPathType, &t_sd_mpeg7_08_xPathRefType, &t_sd_mpeg7_08_xPathSelectorType, &t_sd_mpeg7_08_xPathAbsoluteSelectorType, &t_sd_s08_DomainType, &t_sd_s08_Hexadecimal16bit, &t_sd_s08_Hexadecimal32bit, &t_sd_s08_Hexadecimal3bit, &t_sd_s08_Hexadecimal4bit, &t_sd_s08_Hexadecimal8bit, &t_sd_s08_ServiceType, &t_sd_s08_IPType, &t_sd_s08_ISO_3166_List, &t_sd_s08_ISO639_2, &t_sd_s08_MultilingualType, &t_sd_s08_PackageAvailabilityCountryCodeType, &t_sd_s08_PrimarySISource, &t_sd_s08_Service, &t_sd_s08_StreamingType, &t_sd_s08_TransportProtocolType, &t_sd_s08_Version, &t_sd_s12_BroadcastSystemType, &t_sd_s12_CPSystemIDType, &t_sd_s12_CPSystemSRMIDType, &t_sd_s12_EnhancementServiceType, &t_sd_s12_Usage, &t_sd_tva05_ColorTypeType, &t_sd_tva05_FrameRateType, &t_sd_tva05_RatioType, &t_sd_tva05_AspectRatioType, &t_sd_tva05_ScanType, &t_sd_tva05_SynopsisLengthType, &t_sd_tva05_TVAIDRefType, &t_sd_tva05_TVAIDType, &t_sd_tva05_currencyCodeType, &t_sd_tva05_segmentTypeType, &t_sd_tva05_serviceInformationNameLengthType, &t_sd_tva11_ColorTypeType, &t_sd_tva11_DeliveryModeType, &t_sd_tva11_ExplanationLengthType, &t_sd_tva11_FrameRateType, &t_sd_tva11_RatioType, &t_sd_tva11_AspectRatioType, &t_sd_tva11_ScanType, &t_sd_tva11_SynopsisLengthType, &t_sd_tva11_TVAIDRefType, &t_sd_tva11_TVAIDType, &t_sd_s12_DescriptionLocationBCG, &t_sd_tva11_currencyCodeType, &t_sd_tva11_segmentTypeType, &t_sd_tva11_serviceInformationNameLengthType, &t_sd_mpeg7_TextualBaseType, &t_sd_mpeg7_NameComponentType, &t_sd_mpeg7_TextualType, &t_sd_tva05_KeywordType, &t_sd_tva05_OrganizationNameType, &t_sd_tva05_ServiceInformationNameType, &t_sd_tva05_SynopsisType, &t_sd_tva05_TermNameType, &t_sd_mpeg7_TitleType, &t_sd_tva05_ShortTitleType, &t_sd_mpeg7_UniqueIDType, &t_sd_mpeg7_basicDurationType, &t_sd_mpeg7_durationType, &t_sd_mpeg7_mediaDurationType, &t_sd_mpeg7_mediaTimeOffsetType, &t_sd_mpeg7_MediaRelTimePointType, &t_sd_mpeg7_timeOffsetType, &t_sd_mpeg7_RelTimePointType, &t_sd_mpeg7_basicTimePointType, &t_sd_mpeg7_mediaTimePointType, &t_sd_mpeg7_timePointType, &t_sd_mpeg7_characterSetCode, &t_sd_mpeg7_countryCode, &t_sd_mpeg7_mimeType, &t_sd_mpeg7_regionCode, &t_sd_mpeg7_08_TextualBaseType, &t_sd_mpeg7_08_NameComponentType, &t_sd_mpeg7_08_TextualType, &t_sd_tva11_ExplanationType, &t_sd_tva11_KeywordType, &t_sd_tva11_OrganizationNameType, &t_sd_tva11_ServiceInformationNameType, &t_sd_tva11_SynopsisType, &t_sd_tva11_TermNameType, &t_sd_mpeg7_08_TitleType, &t_sd_tva11_ShortTitleType, &t_sd_mpeg7_08_UniqueIDType, &t_sd_mpeg7_08_basicDurationType, &t_sd_mpeg7_08_durationType, &t_sd_mpeg7_08_mediaDurationType, &t_sd_mpeg7_08_mediaTimeOffsetType, &t_sd_mpeg7_08_MediaRelTimePointType, &t_sd_mpeg7_08_timeOffsetType, &t_sd_mpeg7_08_RelTimePointType, &t_sd_mpeg7_08_basicTimePointType, &t_sd_mpeg7_08_mediaTimePointType, &t_sd_mpeg7_08_timePointType, &t_sd_mpeg7_08_characterSetCode, &t_sd_mpeg7_08_countryCode, &t_sd_mpeg7_08_mimeType, &t_sd_mpeg7_08_regionCode};
static const bim_type_t *const sd_subs_2012_1_17[] = {&t_sd_xs_token, &t_sd_xs_NMTOKEN, &t_sd_mpeg7_userChoiceType, &t_sd_mpeg7_08_userChoiceType, &t_sd_xs_Name, &t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF, &t_sd_xs_language, &t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_tva11_CaptionLanguageType, &t_sd_tva11_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_08_ExtendedLanguageType, &t_sd_tva11_AudioLanguageType, &t_sd_mpeg7_xPathType, &t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType, &t_sd_mpeg7_08_xPathType, &t_sd_mpeg7_08_xPathRefType, &t_sd_mpeg7_08_xPathSelectorType, &t_sd_mpeg7_08_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2012_1_18[] = {&t_sd_xs_NMTOKEN, &t_sd_mpeg7_userChoiceType, &t_sd_mpeg7_08_userChoiceType, &t_sd_xs_Name, &t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF, &t_sd_xs_language, &t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_tva11_CaptionLanguageType, &t_sd_tva11_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_08_ExtendedLanguageType, &t_sd_tva11_AudioLanguageType, &t_sd_mpeg7_xPathType, &t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType, &t_sd_mpeg7_08_xPathType, &t_sd_mpeg7_08_xPathRefType, &t_sd_mpeg7_08_xPathSelectorType, &t_sd_mpeg7_08_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2012_1_19[] = {&t_sd_mpeg7_userChoiceType, &t_sd_mpeg7_08_userChoiceType};
static const bim_type_t *const sd_subs_2012_1_20[] = {&t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF};
static const bim_type_t *const sd_subs_2012_1_21[] = {&t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF};
static const bim_type_t *const sd_subs_2012_1_22[] = {&t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_tva11_CaptionLanguageType, &t_sd_tva11_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_08_ExtendedLanguageType, &t_sd_tva11_AudioLanguageType};
static const bim_type_t *const sd_subs_2012_1_23[] = {&t_sd_tva05_AudioLanguageType};
static const bim_type_t *const sd_subs_2012_1_24[] = {&t_sd_tva11_AudioLanguageType};
static const bim_type_t *const sd_subs_2012_1_25[] = {&t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2012_1_26[] = {&t_sd_mpeg7_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2012_1_27[] = {&t_sd_mpeg7_08_xPathRefType, &t_sd_mpeg7_08_xPathSelectorType, &t_sd_mpeg7_08_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2012_1_28[] = {&t_sd_mpeg7_08_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2012_1_29[] = {&t_sd_s08_ServiceType};
static const bim_type_t *const sd_subs_2012_1_30[] = {&t_sd_tva05_AspectRatioType};
static const bim_type_t *const sd_subs_2012_1_31[] = {&t_sd_tva11_AspectRatioType};
static const bim_type_t *const sd_subs_2012_1_32[] = {&t_sd_s12_DescriptionLocationBCG};
static const bim_type_t *const sd_subs_2012_1_33[] = {&t_sd_mpeg7_NameComponentType, &t_sd_mpeg7_TextualType, &t_sd_tva05_KeywordType, &t_sd_tva05_OrganizationNameType, &t_sd_tva05_ServiceInformationNameType, &t_sd_tva05_SynopsisType, &t_sd_tva05_TermNameType, &t_sd_mpeg7_TitleType, &t_sd_tva05_ShortTitleType};
static const bim_type_t *const sd_subs_2012_1_34[] = {&t_sd_tva05_KeywordType, &t_sd_tva05_OrganizationNameType, &t_sd_tva05_ServiceInformationNameType, &t_sd_tva05_SynopsisType, &t_sd_tva05_TermNameType};
static const bim_type_t *const sd_subs_2012_1_35[] = {&t_sd_tva05_ShortTitleType};
static const bim_type_t *const sd_subs_2012_1_36[] = {&t_sd_mpeg7_durationType, &t_sd_mpeg7_mediaDurationType, &t_sd_mpeg7_mediaTimeOffsetType, &t_sd_mpeg7_MediaRelTimePointType, &t_sd_mpeg7_timeOffsetType, &t_sd_mpeg7_RelTimePointType};
static const bim_type_t *const sd_subs_2012_1_37[] = {&t_sd_mpeg7_MediaRelTimePointType};
static const bim_type_t *const sd_subs_2012_1_38[] = {&t_sd_mpeg7_RelTimePointType};
static const bim_type_t *const sd_subs_2012_1_39[] = {&t_sd_mpeg7_mediaTimePointType, &t_sd_mpeg7_timePointType};
static const bim_type_t *const sd_subs_2012_1_40[] = {&t_sd_mpeg7_08_NameComponentType, &t_sd_mpeg7_08_TextualType, &t_sd_tva11_ExplanationType, &t_sd_tva11_KeywordType, &t_sd_tva11_OrganizationNameType, &t_sd_tva11_ServiceInformationNameType, &t_sd_tva11_SynopsisType, &t_sd_tva11_TermNameType, &t_sd_mpeg7_08_TitleType, &t_sd_tva11_ShortTitleType};
static const bim_type_t *const sd_subs_2012_1_41[] = {&t_sd_tva11_ExplanationType, &t_sd_tva11_KeywordType, &t_sd_tva11_OrganizationNameType, &t_sd_tva11_ServiceInformationNameType, &t_sd_tva11_SynopsisType, &t_sd_tva11_TermNameType};
static const bim_type_t *const sd_subs_2012_1_42[] = {&t_sd_tva11_ShortTitleType};
static const bim_type_t *const sd_subs_2012_1_43[] = {&t_sd_mpeg7_08_durationType, &t_sd_mpeg7_08_mediaDurationType, &t_sd_mpeg7_08_mediaTimeOffsetType, &t_sd_mpeg7_08_MediaRelTimePointType, &t_sd_mpeg7_08_timeOffsetType, &t_sd_mpeg7_08_RelTimePointType};
static const bim_type_t *const sd_subs_2012_1_44[] = {&t_sd_mpeg7_08_MediaRelTimePointType};
static const bim_type_t *const sd_subs_2012_1_45[] = {&t_sd_mpeg7_08_RelTimePointType};
static const bim_type_t *const sd_subs_2012_1_46[] = {&t_sd_mpeg7_08_mediaTimePointType, &t_sd_mpeg7_08_timePointType};
static const bim_type_t *const sd_subs_2012_1_47[] = {&t_sd_s12_PackageTextualIdentifier};
static const bim_type_t *const sd_subs_2012_1_48[] = {&t_sd_s12_SRMIDVerMType, &t_sd_s12_SRMIDVerUType};
static const bim_type_t *const sd_subs_2012_1_49[] = {&t_sd_tva05_GenreType};
static const bim_type_t *const sd_subs_2012_1_50[] = {&t_sd_tva11_GenreType};
static const bim_subtypes_t sd_subtypes_2012_1[] = {
  {&t_sd_xs_anyURI, sd_subs_2012_1_0, 9, NULL},
  {&t_sd_s08_DescriptionLocation, sd_subs_2012_1_1, 1, NULL},
  {&t_sd_s08_RTSP, sd_subs_2012_1_2, 1, NULL},
  {&t_sd_xs_decimal, sd_subs_2012_1_3, 36, NULL},
  {&t_sd_xs_integer, sd_subs_2012_1_4, 35, NULL},
  {&t_sd_xs_long, sd_subs_2012_1_5, 4, NULL},
  {&t_sd_xs_int, sd_subs_2012_1_6, 3, NULL},
  {&t_sd_xs_short, sd_subs_2012_1_7, 2, NULL},
  {&t_sd_xs_byte, sd_subs_2012_1_8, 1, NULL},
  {&t_sd_xs_nonNegativeInteger, sd_subs_2012_1_9, 15, NULL},
  {&t_sd_xs_unsignedLong, sd_subs_2012_1_10, 7, NULL},
  {&t_sd_xs_unsignedInt, sd_subs_2012_1_11, 6, NULL},
  {&t_sd_xs_unsignedShort, sd_subs_2012_1_12, 5, NULL},
  {&t_sd_xs_nonPositiveInteger, sd_subs_2012_1_13, 1, NULL},
  {&t_sd_mpeg7_MediaRelIncrTimePointType, sd_subs_2012_1_14, 1, NULL},
  {&t_sd_mpeg7_08_MediaRelIncrTimePointType, sd_subs_2012_1_15, 1, NULL},
  {&t_sd_xs_string, sd_subs_2012_1_16, 125, NULL},
  {&t_sd_xs_normalizedString, sd_subs_2012_1_17, 26, NULL},
  {&t_sd_xs_token, sd_subs_2012_1_18, 25, NULL},
  {&t_sd_xs_NMTOKEN, sd_subs_2012_1_19, 2, NULL},
  {&t_sd_xs_Name, sd_subs_2012_1_20, 4, NULL},
  {&t_sd_xs_NCName, sd_subs_2012_1_21, 3, NULL},
  {&t_sd_xs_language, sd_subs_2012_1_22, 8, NULL},
  {&t_sd_mpeg7_ExtendedLanguageType, sd_subs_2012_1_23, 1, NULL},
  {&t_sd_mpeg7_08_ExtendedLanguageType, sd_subs_2012_1_24, 1, NULL},
  {&t_sd_mpeg7_xPathType, sd_subs_2012_1_25, 3, NULL},
  {&t_sd_mpeg7_xPathSelectorType, sd_subs_2012_1_26, 1, NULL},
  {&t_sd_mpeg7_08_xPathType, sd_subs_2012_1_27, 3, NULL},
  {&t_sd_mpeg7_08_xPathSelectorType, sd_subs_2012_1_28, 1, NULL},
  {&t_sd_s08_Hexadecimal8bit, sd_subs_2012_1_29, 1, NULL},
  {&t_sd_tva05_RatioType, sd_subs_2012_1_30, 1, NULL},
  {&t_sd_tva11_RatioType, sd_subs_2012_1_31, 1, NULL},
  {&t_sd_tva11_TVAIDType, sd_subs_2012_1_32, 1, NULL},
  {&t_sd_mpeg7_TextualBaseType, sd_subs_2012_1_33, 9, NULL},
  {&t_sd_mpeg7_TextualType, sd_subs_2012_1_34, 5, NULL},
  {&t_sd_mpeg7_TitleType, sd_subs_2012_1_35, 1, NULL},
  {&t_sd_mpeg7_basicDurationType, sd_subs_2012_1_36, 6, NULL},
  {&t_sd_mpeg7_mediaTimeOffsetType, sd_subs_2012_1_37, 1, NULL},
  {&t_sd_mpeg7_timeOffsetType, sd_subs_2012_1_38, 1, NULL},
  {&t_sd_mpeg7_basicTimePointType, sd_subs_2012_1_39, 2, NULL},
  {&t_sd_mpeg7_08_TextualBaseType, sd_subs_2012_1_40, 10, NULL},
  {&t_sd_mpeg7_08_TextualType, sd_subs_2012_1_41, 6, NULL},
  {&t_sd_mpeg7_08_TitleType, sd_subs_2012_1_42, 1, NULL},
  {&t_sd_mpeg7_08_basicDurationType, sd_subs_2012_1_43, 6, NULL},
  {&t_sd_mpeg7_08_mediaTimeOffsetType, sd_subs_2012_1_44, 1, NULL},
  {&t_sd_mpeg7_08_timeOffsetType, sd_subs_2012_1_45, 1, NULL},
  {&t_sd_mpeg7_08_basicTimePointType, sd_subs_2012_1_46, 2, NULL},
  {&t_sd_s08_TextualIdentifier, sd_subs_2012_1_47, 1, NULL},
  {&t_sd_s12_SRMIDType, sd_subs_2012_1_48, 2, NULL},
  {&t_sd_tva05_ControlledTermType, sd_subs_2012_1_49, 1, NULL},
  {&t_sd_tva11_ControlledTermType, sd_subs_2012_1_50, 1, NULL}
};
static const sdns_prefix_t sd_prefixes_2012_1[] = {{"s08", ns_s08}, {"tva05", ns_tva05}, {"tva11", ns_tva11}};
static const bim_type_t *const sd_subs_2008_1_0[] = {&t_sd_s08_DescriptionLocation, &t_sd_s08_DescriptionLocationBCG, &t_sd_s08_PullURL, &t_sd_s08_RTSP, &t_sd_s08_RTSPURLType, &t_sd_tva05_CRIDType, &t_sd_tva05_InstanceMetadataIdType};
static const bim_type_t *const sd_subs_2008_1_1[] = {&t_sd_s08_DescriptionLocationBCG};
static const bim_type_t *const sd_subs_2008_1_2[] = {&t_sd_s08_RTSPURLType};
static const bim_type_t *const sd_subs_2008_1_3[] = {&t_sd_xs_integer, &t_sd_xs_long, &t_sd_xs_int, &t_sd_xs_short, &t_sd_xs_byte, &t_sd_s08_Genre, &t_sd_xs_nonNegativeInteger, &t_sd_xs_positiveInteger, &t_sd_xs_unsignedLong, &t_sd_xs_unsignedInt, &t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId, &t_sd_tva05_BitRateType, &t_sd_mpeg7_unsigned12, &t_sd_mpeg7_unsigned8, &t_sd_xs_nonPositiveInteger, &t_sd_xs_negativeInteger, &t_sd_mpeg7_IncrDurationType, &t_sd_mpeg7_MediaIncrDurationType, &t_sd_mpeg7_MediaRelIncrTimePointType, &t_sd_tva05_TVAMediaRelIncrTimePointType, &t_sd_mpeg7_RelIncrTimePointType, &t_sd_mpeg7_preferenceValueType};
static const bim_type_t *const sd_subs_2008_1_4[] = {&t_sd_xs_long, &t_sd_xs_int, &t_sd_xs_short, &t_sd_xs_byte, &t_sd_s08_Genre, &t_sd_xs_nonNegativeInteger, &t_sd_xs_positiveInteger, &t_sd_xs_unsignedLong, &t_sd_xs_unsignedInt, &t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId, &t_sd_tva05_BitRateType, &t_sd_mpeg7_unsigned12, &t_sd_mpeg7_unsigned8, &t_sd_xs_nonPositiveInteger, &t_sd_xs_negativeInteger, &t_sd_mpeg7_IncrDurationType, &t_sd_mpeg7_MediaIncrDurationType, &t_sd_mpeg7_MediaRelIncrTimePointType, &t_sd_tva05_TVAMediaRelIncrTimePointType, &t_sd_mpeg7_RelIncrTimePointType, &t_sd_mpeg7_preferenceValueType};
static const bim_type_t *const sd_subs_2008_1_5[] = {&t_sd_xs_int, &t_sd_xs_short, &t_sd_xs_byte, &t_sd_s08_Genre};
static const bim_type_t *const sd_subs_2008_1_6[] = {&t_sd_xs_short, &t_sd_xs_byte, &t_sd_s08_Genre};
static const bim_type_t *const sd_subs_2008_1_7[] = {&t_sd_xs_byte, &t_sd_s08_Genre};
static const bim_type_t *const sd_subs_2008_1_8[] = {&t_sd_s08_Genre};
static const bim_type_t *const sd_subs_2008_1_9[] = {&t_sd_xs_positiveInteger, &t_sd_xs_unsignedLong, &t_sd_xs_unsignedInt, &t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId, &t_sd_tva05_BitRateType, &t_sd_mpeg7_unsigned12, &t_sd_mpeg7_unsigned8};
static const bim_type_t *const sd_subs_2008_1_10[] = {&t_sd_xs_unsignedInt, &t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId};
static const bim_type_t *const sd_subs_2008_1_11[] = {&t_sd_xs_unsignedShort, &t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId};
static const bim_type_t *const sd_subs_2008_1_12[] = {&t_sd_xs_unsignedByte, &t_sd_s08_Integer6bit, &t_sd_s08_OrigNetId, &t_sd_s08_ServiceId, &t_sd_s08_TSId};
static const bim_type_t *const sd_subs_2008_1_13[] = {&t_sd_xs_negativeInteger};
static const bim_type_t *const sd_subs_2008_1_14[] = {&t_sd_tva05_TVAMediaRelIncrTimePointType};
static const bim_type_t *const sd_subs_2008_1_15[] = {&t_sd_xs_normalizedString, &t_sd_xs_token, &t_sd_xs_NMTOKEN, &t_sd_mpeg7_userChoiceType, &t_sd_xs_Name, &t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF, &t_sd_xs_language, &t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_xPathType, &t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType, &t_sd_s08_DomainType, &t_sd_s08_Hexadecimal16bit, &t_sd_s08_Hexadecimal32bit, &t_sd_s08_Hexadecimal3bit, &t_sd_s08_Hexadecimal4bit, &t_sd_s08_Hexadecimal8bit, &t_sd_s08_ServiceType, &t_sd_s08_IPType, &t_sd_s08_ISO_3166_List, &t_sd_s08_ISO639_2, &t_sd_s08_MultilingualType, &t_sd_s08_PackageAvailabilityCountryCodeType, &t_sd_s08_PrimarySISource, &t_sd_s08_Service, &t_sd_s08_StreamingType, &t_sd_s08_TransportProtocolType, &t_sd_s08_Version, &t_sd_tva05_ColorTypeType, &t_sd_tva05_FrameRateType, &t_sd_tva05_RatioType, &t_sd_tva05_AspectRatioType, &t_sd_tva05_ScanType, &t_sd_tva05_SynopsisLengthType, &t_sd_tva05_TVAIDRefType, &t_sd_tva05_TVAIDType, &t_sd_tva05_currencyCodeType, &t_sd_tva05_segmentTypeType, &t_sd_tva05_serviceInformationNameLengthType, &t_sd_mpeg7_TextualBaseType, &t_sd_mpeg7_NameComponentType, &t_sd_mpeg7_TextualType, &t_sd_tva05_KeywordType, &t_sd_tva05_OrganizationNameType, &t_sd_tva05_ServiceInformationNameType, &t_sd_tva05_SynopsisType, &t_sd_tva05_TermNameType, &t_sd_mpeg7_TitleType, &t_sd_tva05_ShortTitleType, &t_sd_mpeg7_UniqueIDType, &t_sd_mpeg7_basicDurationType, &t_sd_mpeg7_durationType, &t_sd_mpeg7_mediaDurationType, &t_sd_mpeg7_mediaTimeOffsetType, &t_sd_mpeg7_MediaRelTimePointType, &t_sd_mpeg7_timeOffsetType, &t_sd_mpeg7_RelTimePointType, &t_sd_mpeg7_basicTimePointType, &t_sd_mpeg7_mediaTimePointType, &t_sd_mpeg7_timePointType, &t_sd_mpeg7_characterSetCode, &t_sd_mpeg7_countryCode, &t_sd_mpeg7_mimeType, &t_sd_mpeg7_regionCode};
static const bim_type_t *const sd_subs_2008_1_16[] = {&t_sd_xs_token, &t_sd_xs_NMTOKEN, &t_sd_mpeg7_userChoiceType, &t_sd_xs_Name, &t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF, &t_sd_xs_language, &t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_xPathType, &t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2008_1_17[] = {&t_sd_xs_NMTOKEN, &t_sd_mpeg7_userChoiceType, &t_sd_xs_Name, &t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF, &t_sd_xs_language, &t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_xPathType, &t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2008_1_18[] = {&t_sd_mpeg7_userChoiceType};
static const bim_type_t *const sd_subs_2008_1_19[] = {&t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF};
static const bim_type_t *const sd_subs_2008_1_20[] = {&t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF};
static const bim_type_t *const sd_subs_2008_1_21[] = {&t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType};
static const bim_type_t *const sd_subs_2008_1_22[] = {&t_sd_tva05_AudioLanguageType};
static const bim_type_t *const sd_subs_2008_1_23[] = {&t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2008_1_24[] = {&t_sd_mpeg7_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2008_1_25[] = {&t_sd_s08_ServiceType};
static const bim_type_t *const sd_subs_2008_1_26[] = {&t_sd_tva05_AspectRatioType};
static const bim_type_t *const sd_subs_2008_1_27[] = {&t_sd_mpeg7_NameComponentType, &t_sd_mpeg7_TextualType, &t_sd_tva05_KeywordType, &t_sd_tva05_OrganizationNameType, &t_sd_tva05_ServiceInformationNameType, &t_sd_tva05_SynopsisType, &t_sd_tva05_TermNameType, &t_sd_mpeg7_TitleType, &t_sd_tva05_ShortTitleType};
static const bim_type_t *const sd_subs_2008_1_28[] = {&t_sd_tva05_KeywordType, &t_sd_tva05_OrganizationNameType, &t_sd_tva05_ServiceInformationNameType, &t_sd_tva05_SynopsisType, &t_sd_tva05_TermNameType};
static const bim_type_t *const sd_subs_2008_1_29[] = {&t_sd_tva05_ShortTitleType};
static const bim_type_t *const sd_subs_2008_1_30[] = {&t_sd_mpeg7_durationType, &t_sd_mpeg7_mediaDurationType, &t_sd_mpeg7_mediaTimeOffsetType, &t_sd_mpeg7_MediaRelTimePointType, &t_sd_mpeg7_timeOffsetType, &t_sd_mpeg7_RelTimePointType};
static const bim_type_t *const sd_subs_2008_1_31[] = {&t_sd_mpeg7_MediaRelTimePointType};
static const bim_type_t *const sd_subs_2008_1_32[] = {&t_sd_mpeg7_RelTimePointType};
static const bim_type_t *const sd_subs_2008_1_33[] = {&t_sd_mpeg7_mediaTimePointType, &t_sd_mpeg7_timePointType};
static const bim_type_t *const sd_subs_2008_1_34[] = {&t_sd_tva05_GenreType};
static const bim_subtypes_t sd_subtypes_2008_1[] = {
  {&t_sd_xs_anyURI, sd_subs_2008_1_0, 7, NULL},
  {&t_sd_s08_DescriptionLocation, sd_subs_2008_1_1, 1, NULL},
  {&t_sd_s08_RTSP, sd_subs_2008_1_2, 1, NULL},
  {&t_sd_xs_decimal, sd_subs_2008_1_3, 27, NULL},
  {&t_sd_xs_integer, sd_subs_2008_1_4, 26, NULL},
  {&t_sd_xs_long, sd_subs_2008_1_5, 4, NULL},
  {&t_sd_xs_int, sd_subs_2008_1_6, 3, NULL},
  {&t_sd_xs_short, sd_subs_2008_1_7, 2, NULL},
  {&t_sd_xs_byte, sd_subs_2008_1_8, 1, NULL},
  {&t_sd_xs_nonNegativeInteger, sd_subs_2008_1_9, 12, NULL},
  {&t_sd_xs_unsignedLong, sd_subs_2008_1_10, 7, NULL},
  {&t_sd_xs_unsignedInt, sd_subs_2008_1_11, 6, NULL},
  {&t_sd_xs_unsignedShort, sd_subs_2008_1_12, 5, NULL},
  {&t_sd_xs_nonPositiveInteger, sd_subs_2008_1_13, 1, NULL},
  {&t_sd_mpeg7_MediaRelIncrTimePointType, sd_subs_2008_1_14, 1, NULL},
  {&t_sd_xs_string, sd_subs_2008_1_15, 71, NULL},
  {&t_sd_xs_normalizedString, sd_subs_2008_1_16, 17, NULL},
  {&t_sd_xs_token, sd_subs_2008_1_17, 16, NULL},
  {&t_sd_xs_NMTOKEN, sd_subs_2008_1_18, 1, NULL},
  {&t_sd_xs_Name, sd_subs_2008_1_19, 4, NULL},
  {&t_sd_xs_NCName, sd_subs_2008_1_20, 3, NULL},
  {&t_sd_xs_language, sd_subs_2008_1_21, 4, NULL},
  {&t_sd_mpeg7_ExtendedLanguageType, sd_subs_2008_1_22, 1, NULL},
  {&t_sd_mpeg7_xPathType, sd_subs_2008_1_23, 3, NULL},
  {&t_sd_mpeg7_xPathSelectorType, sd_subs_2008_1_24, 1, NULL},
  {&t_sd_s08_Hexadecimal8bit, sd_subs_2008_1_25, 1, NULL},
  {&t_sd_tva05_RatioType, sd_subs_2008_1_26, 1, NULL},
  {&t_sd_mpeg7_TextualBaseType, sd_subs_2008_1_27, 9, NULL},
  {&t_sd_mpeg7_TextualType, sd_subs_2008_1_28, 5, NULL},
  {&t_sd_mpeg7_TitleType, sd_subs_2008_1_29, 1, NULL},
  {&t_sd_mpeg7_basicDurationType, sd_subs_2008_1_30, 6, NULL},
  {&t_sd_mpeg7_mediaTimeOffsetType, sd_subs_2008_1_31, 1, NULL},
  {&t_sd_mpeg7_timeOffsetType, sd_subs_2008_1_32, 1, NULL},
  {&t_sd_mpeg7_basicTimePointType, sd_subs_2008_1_33, 2, NULL},
  {&t_sd_tva05_ControlledTermType, sd_subs_2008_1_34, 1, NULL}
};
static const sdns_prefix_t sd_prefixes_2008_1[] = {{"tva05", ns_tva05}};
static const bim_type_t *const sd_subs_2006_0[] = {&t_sd_s06_DescriptionLocation, &t_sd_s06_DescriptionLocationBCG, &t_sd_s06_PullURL, &t_sd_s06_RTSP, &t_sd_tva05_CRIDType, &t_sd_tva05_InstanceMetadataIdType};
static const bim_type_t *const sd_subs_2006_1[] = {&t_sd_s06_DescriptionLocationBCG};
static const bim_type_t *const sd_subs_2006_2[] = {&t_sd_xs_unsignedByte, &t_sd_s06_Integer6bit, &t_sd_s06_OrigNetId, &t_sd_s06_ServiceId, &t_sd_s06_TSId};
static const bim_type_t *const sd_subs_2006_3[] = {&t_sd_xs_normalizedString, &t_sd_xs_token, &t_sd_xs_NMTOKEN, &t_sd_mpeg7_userChoiceType, &t_sd_xs_Name, &t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF, &t_sd_xs_language, &t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_xPathType, &t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType, &t_sd_s06_DomainType, &t_sd_s06_Hexadecimal16bit, &t_sd_s06_Hexadecimal3bit, &t_sd_s06_Hexadecimal4bit, &t_sd_s06_Hexadecimal8bit, &t_sd_s06_ServiceType, &t_sd_s06_IPType, &t_sd_s06_ISO_3166_List, &t_sd_s06_ISO639_2, &t_sd_s06_MultilingualType, &t_sd_s06_PackageAvailabilityCountryCodeType, &t_sd_s06_PrimarySISource, &t_sd_s06_Service, &t_sd_s06_StreamingType, &t_sd_tva05_ColorTypeType, &t_sd_tva05_FrameRateType, &t_sd_tva05_RatioType, &t_sd_tva05_AspectRatioType, &t_sd_tva05_ScanType, &t_sd_tva05_SynopsisLengthType, &t_sd_tva05_TVAIDRefType, &t_sd_tva05_TVAIDType, &t_sd_tva05_currencyCodeType, &t_sd_tva05_segmentTypeType, &t_sd_tva05_serviceInformationNameLengthType, &t_sd_mpeg7_TextualBaseType, &t_sd_mpeg7_NameComponentType, &t_sd_mpeg7_TextualType, &t_sd_tva05_KeywordType, &t_sd_tva05_OrganizationNameType, &t_sd_tva05_ServiceInformationNameType, &t_sd_tva05_SynopsisType, &t_sd_tva05_TermNameType, &t_sd_mpeg7_TitleType, &t_sd_tva05_ShortTitleType, &t_sd_mpeg7_UniqueIDType, &t_sd_mpeg7_basicDurationType, &t_sd_mpeg7_durationType, &t_sd_mpeg7_mediaDurationType, &t_sd_mpeg7_mediaTimeOffsetType, &t_sd_mpeg7_MediaRelTimePointType, &t_sd_mpeg7_timeOffsetType, &t_sd_mpeg7_RelTimePointType, &t_sd_mpeg7_basicTimePointType, &t_sd_mpeg7_mediaTimePointType, &t_sd_mpeg7_timePointType, &t_sd_mpeg7_characterSetCode, &t_sd_mpeg7_countryCode, &t_sd_mpeg7_mimeType, &t_sd_mpeg7_regionCode};
static const bim_type_t *const sd_subs_2006_4[] = {&t_sd_xs_token, &t_sd_xs_NMTOKEN, &t_sd_mpeg7_userChoiceType, &t_sd_xs_Name, &t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF, &t_sd_xs_language, &t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_xPathType, &t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2006_5[] = {&t_sd_xs_NMTOKEN, &t_sd_mpeg7_userChoiceType, &t_sd_xs_Name, &t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF, &t_sd_xs_language, &t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType, &t_sd_mpeg7_xPathType, &t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2006_6[] = {&t_sd_mpeg7_userChoiceType};
static const bim_type_t *const sd_subs_2006_7[] = {&t_sd_xs_NCName, &t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF};
static const bim_type_t *const sd_subs_2006_8[] = {&t_sd_xs_ENTITY, &t_sd_xs_ID, &t_sd_xs_IDREF};
static const bim_type_t *const sd_subs_2006_9[] = {&t_sd_tva05_CaptionLanguageType, &t_sd_tva05_SignLanguageType, &t_sd_mpeg7_ExtendedLanguageType, &t_sd_tva05_AudioLanguageType};
static const bim_type_t *const sd_subs_2006_10[] = {&t_sd_tva05_AudioLanguageType};
static const bim_type_t *const sd_subs_2006_11[] = {&t_sd_mpeg7_xPathRefType, &t_sd_mpeg7_xPathSelectorType, &t_sd_mpeg7_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2006_12[] = {&t_sd_mpeg7_xPathAbsoluteSelectorType};
static const bim_type_t *const sd_subs_2006_13[] = {&t_sd_s06_ServiceType};
static const bim_type_t *const sd_subs_2006_14[] = {&t_sd_tva05_AspectRatioType};
static const bim_type_t *const sd_subs_2006_15[] = {&t_sd_mpeg7_NameComponentType, &t_sd_mpeg7_TextualType, &t_sd_tva05_KeywordType, &t_sd_tva05_OrganizationNameType, &t_sd_tva05_ServiceInformationNameType, &t_sd_tva05_SynopsisType, &t_sd_tva05_TermNameType, &t_sd_mpeg7_TitleType, &t_sd_tva05_ShortTitleType};
static const bim_type_t *const sd_subs_2006_16[] = {&t_sd_tva05_KeywordType, &t_sd_tva05_OrganizationNameType, &t_sd_tva05_ServiceInformationNameType, &t_sd_tva05_SynopsisType, &t_sd_tva05_TermNameType};
static const bim_type_t *const sd_subs_2006_17[] = {&t_sd_tva05_ShortTitleType};
static const bim_type_t *const sd_subs_2006_18[] = {&t_sd_mpeg7_durationType, &t_sd_mpeg7_mediaDurationType, &t_sd_mpeg7_mediaTimeOffsetType, &t_sd_mpeg7_MediaRelTimePointType, &t_sd_mpeg7_timeOffsetType, &t_sd_mpeg7_RelTimePointType};
static const bim_type_t *const sd_subs_2006_19[] = {&t_sd_mpeg7_MediaRelTimePointType};
static const bim_type_t *const sd_subs_2006_20[] = {&t_sd_mpeg7_RelTimePointType};
static const bim_type_t *const sd_subs_2006_21[] = {&t_sd_mpeg7_mediaTimePointType, &t_sd_mpeg7_timePointType};
static const bim_type_t *const sd_subs_2006_22[] = {&t_sd_tva05_GenreType};
static const bim_subtypes_t sd_subtypes_2006[] = {
  {&t_sd_xs_anyURI, sd_subs_2006_0, 6, NULL},
  {&t_sd_s06_DescriptionLocation, sd_subs_2006_1, 1, NULL},
  {&t_sd_xs_unsignedShort, sd_subs_2006_2, 5, NULL},
  {&t_sd_xs_string, sd_subs_2006_3, 68, NULL},
  {&t_sd_xs_normalizedString, sd_subs_2006_4, 17, NULL},
  {&t_sd_xs_token, sd_subs_2006_5, 16, NULL},
  {&t_sd_xs_NMTOKEN, sd_subs_2006_6, 1, NULL},
  {&t_sd_xs_Name, sd_subs_2006_7, 4, NULL},
  {&t_sd_xs_NCName, sd_subs_2006_8, 3, NULL},
  {&t_sd_xs_language, sd_subs_2006_9, 4, NULL},
  {&t_sd_mpeg7_ExtendedLanguageType, sd_subs_2006_10, 1, NULL},
  {&t_sd_mpeg7_xPathType, sd_subs_2006_11, 3, NULL},
  {&t_sd_mpeg7_xPathSelectorType, sd_subs_2006_12, 1, NULL},
  {&t_sd_s06_Hexadecimal8bit, sd_subs_2006_13, 1, NULL},
  {&t_sd_tva05_RatioType, sd_subs_2006_14, 1, NULL},
  {&t_sd_mpeg7_TextualBaseType, sd_subs_2006_15, 9, NULL},
  {&t_sd_mpeg7_TextualType, sd_subs_2006_16, 5, NULL},
  {&t_sd_mpeg7_TitleType, sd_subs_2006_17, 1, NULL},
  {&t_sd_mpeg7_basicDurationType, sd_subs_2006_18, 6, NULL},
  {&t_sd_mpeg7_mediaTimeOffsetType, sd_subs_2006_19, 1, NULL},
  {&t_sd_mpeg7_timeOffsetType, sd_subs_2006_20, 1, NULL},
  {&t_sd_mpeg7_basicTimePointType, sd_subs_2006_21, 2, NULL},
  {&t_sd_tva05_ControlledTermType, sd_subs_2006_22, 1, NULL}
};
static const sdns_prefix_t sd_prefixes_2006[] = {{"tva05", ns_tva05}};

const sdns_set_t sdns_sets[] = {
  {ns_s14, &t_sd_ServiceDiscovery, sd_subtypes_2014_1, (int)(sizeof sd_subtypes_2014_1 / sizeof sd_subtypes_2014_1[0]), sd_prefixes_2014_1,
   (int)(sizeof sd_prefixes_2014_1 / sizeof sd_prefixes_2014_1[0])},
  {ns_s12, &t_sd_ServiceDiscovery_2, sd_subtypes_2012_1, (int)(sizeof sd_subtypes_2012_1 / sizeof sd_subtypes_2012_1[0]), sd_prefixes_2012_1,
   (int)(sizeof sd_prefixes_2012_1 / sizeof sd_prefixes_2012_1[0])},
  {ns_s08, &t_sd_ServiceDiscovery_3, sd_subtypes_2008_1, (int)(sizeof sd_subtypes_2008_1 / sizeof sd_subtypes_2008_1[0]), sd_prefixes_2008_1,
   (int)(sizeof sd_prefixes_2008_1 / sizeof sd_prefixes_2008_1[0])},
  {ns_s06, &t_sd_ServiceDiscovery_4, sd_subtypes_2006, (int)(sizeof sd_subtypes_2006 / sizeof sd_subtypes_2006[0]), sd_prefixes_2006,
   (int)(sizeof sd_prefixes_2006 / sizeof sd_prefixes_2006[0])},
};
const int sdns_nsets = (int)(sizeof sdns_sets / sizeof sdns_sets[0]);
