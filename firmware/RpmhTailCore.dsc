## Isolated diagnostic core with the accepted RELEASE core's boot libraries.
[Defines]
  PLATFORM_NAME = PoolLifetimeCore
  PLATFORM_GUID = 2401BD95-43D7-4349-9E0A-CA00CE300401
  PLATFORM_VERSION = 0.1
  DSC_SPECIFICATION = 0x0001001C
  OUTPUT_DIRECTORY = build/PoolLifetimeCore
  SUPPORTED_ARCHITECTURES = AARCH64
  BUILD_TARGETS = RELEASE
  SKUID_IDENTIFIER = DEFAULT

!include MdePkg/MdeLibs.dsc.inc

[LibraryClasses]
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  CacheMaintenanceLib|ArmPkg/Library/ArmCacheMaintenanceLib/ArmCacheMaintenanceLib.inf
  UefiDecompressLib|MdePkg/Library/BaseUefiDecompressLib/BaseUefiDecompressLib.inf
  PerformanceLib|MdeModulePkg/Library/DxeCorePerformanceLib/DxeCorePerformanceLib.inf
  HobLib|MdePkg/Library/DxeCoreHobLib/DxeCoreHobLib.inf
  MemoryAllocationLib|MdeModulePkg/Library/DxeCoreMemoryAllocationLib/DxeCoreMemoryAllocationLib.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  SerialPortLib|MdePkg/Library/BaseSerialPortLibNull/BaseSerialPortLibNull.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  DxeCoreEntryPoint|MdePkg/Library/DxeCoreEntryPoint/DxeCoreEntryPoint.inf
  PeCoffLib|MdePkg/Library/BasePeCoffLib/BasePeCoffLib.inf
  PeCoffGetEntryPointLib|MdePkg/Library/BasePeCoffGetEntryPointLib/BasePeCoffGetEntryPointLib.inf
  PeCoffExtraActionLib|MdePkg/Library/BasePeCoffExtraActionLibNull/BasePeCoffExtraActionLibNull.inf
  ExtractGuidedSectionLib|MdePkg/Library/DxeExtractGuidedSectionLib/DxeExtractGuidedSectionLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  ReportStatusCodeLib|MdeModulePkg/Library/DxeReportStatusCodeLib/DxeReportStatusCodeLib.inf
  DxeServicesLib|MdePkg/Library/DxeServicesLib/DxeServicesLib.inf
  DebugAgentLib|MdeModulePkg/Library/DebugAgentLibNull/DebugAgentLibNull.inf
  DebugAgentTimerLib|EmbeddedPkg/Library/DebugAgentTimerLibNull/DebugAgentTimerLibNull.inf
  CpuExceptionHandlerLib|ArmPkg/Library/ArmExceptionLib/ArmExceptionLib.inf
  DefaultExceptionHandlerLib|ArmPkg/Library/DefaultExceptionHandlerLib/DefaultExceptionHandlerLib.inf
  ArmLib|ArmPkg/Library/ArmLib/ArmBaseLib.inf
  TimerLib|ArmPkg/Library/ArmArchTimerLib/ArmArchTimerLib.inf
  ArmGenericTimerCounterLib|ArmPkg/Library/ArmGenericTimerPhyCounterLib/ArmGenericTimerPhyCounterLib.inf
  PcdLib|MdePkg/Library/DxePcdLib/DxePcdLib.inf
  ImagePropertiesRecordLib|MdeModulePkg/Library/ImagePropertiesRecordLib/ImagePropertiesRecordLib.inf
  DxeMemoryProtectionHobLib|MdeModulePkg/Library/MemoryProtectionHobLib/DxeMemoryProtectionHobLib.inf

[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0
  gEfiMdePkgTokenSpaceGuid.PcdDebugClearMemoryValue|0
  gEfiMdePkgTokenSpaceGuid.PcdPerformanceLibraryPropertyMask|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdMemoryProfilePropertyMask|0
  gEfiMdePkgTokenSpaceGuid.PcdUefiLibMaxPrintBufferSize|16000

[PcdsFeatureFlag]
  gEfiMdeModulePkgTokenSpaceGuid.PcdInternalEventServicesEnabled|FALSE
  gEfiMdePkgTokenSpaceGuid.PcdDriverDiagnosticsDisable|TRUE
  gEfiMdePkgTokenSpaceGuid.PcdDriverDiagnostics2Disable|TRUE
  gEfiMdePkgTokenSpaceGuid.PcdComponentNameDisable|TRUE
  gEfiMdePkgTokenSpaceGuid.PcdComponentName2Disable|TRUE

[Components]
  MdeModulePkg/Core/Dxe/DxeMain.inf {
    <LibraryClasses>
    NULL|MdeModulePkg/Library/DxeCrc32GuidedSectionExtractLib/DxeCrc32GuidedSectionExtractLib.inf
    NULL|MdeModulePkg/Library/DxeCrc32GuidedSectionExtractLib/DxeCrc32GuidedSectionExtractLib.inf
    NULL|MdeModulePkg/Library/LzmaCustomDecompressLib/LzmaCustomDecompressLib.inf
  }
