#include <stdarg.h>
#include "map-probe-host-tests.c"
#include "CepheusSmemMapDriver.c"

STATIC EFI_LOADED_IMAGE_PROTOCOL Loaded;
STATIC ARM_MEMORY_REGION_DESCRIPTOR_EX Map[MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT];
STATIC unsigned Handles, DebugLines;
STATIC EFI_STATUS HandleStatus;
STATIC char Log[65536];
STATIC size_t LogBytes;

UINTN EFIAPI AsciiPrint (CONST CHAR8 *Format, ...)
{
  char Converted[512]; size_t N = 0;
  for (size_t I = 0; Format[I]; I++) {
    if (Format[I] == '%' && Format[I + 1] == 'a') {
      Converted[N++] = '%'; Converted[N++] = 's'; I++;
    } else if (Format[I] == '%' && Format[I + 1] == 'l' && Format[I + 2] == 'x') {
      memcpy (Converted + N, "%llx", 4); N += 4; I += 2;
    } else { Converted[N++] = Format[I]; }
    if (N >= sizeof (Converted) - 4) { abort (); }
  }
  Converted[N] = 0;
  va_list Args; va_start (Args, Format);
  int Bytes = vsnprintf (Log + LogBytes, sizeof (Log) - LogBytes, Converted, Args);
  va_end (Args);
  if (Bytes < 0 || (size_t)Bytes >= sizeof (Log) - LogBytes) { abort (); }
  LogBytes += (size_t)Bytes; DebugLines++;
  return (UINTN)Bytes;
}
ARM_MEMORY_REGION_DESCRIPTOR_EX *GetPlatformMemoryMap (VOID) { return Map; }
STATIC EFI_STATUS EFIAPI Handle (EFI_HANDLE Image, EFI_GUID *Guid, VOID **Output)
{
  (VOID)Image; Handles++;
  if (Guid != &gEfiLoadedImageProtocolGuid) { abort (); }
  if (!EFI_ERROR (HandleStatus)) { *Output = &Loaded; }
  return HandleStatus;
}
EFI_GUID gEfiLoadedImageProtocolGuid;
STATIC VOID DriverFixture (VOID)
{
  Fixture (); memset (&Loaded, 0, sizeof (Loaded)); memset (Map, 0, sizeof (Map));
  ZeroMem (&mSmemEvidence, sizeof (mSmemEvidence)); mSmemDriverAttempted = FALSE;
  Map[0] = Profile; Bs.HandleProtocol = Handle;
  Handles = DebugLines = 0; LogBytes = 0; Log[0] = 0; HandleStatus = EFI_SUCCESS;
  STATIC CONST CHAR16 Option[] = L"cephsmemmap=1";
  Loaded.LoadOptions = (VOID *)Option; Loaded.LoadOptionsSize = sizeof (Option);
}
int main (void)
{
  DriverFixture (); CHECK (!CepheusSmemMapDriverEntry ((VOID *)1, NULL));
  CHECK (Pars == 512 && Creates == 1 && Closes == 1 && DebugLines == 518);
  CHECK (strstr (Log, "CEPHSMEMMAP end probe=0 close=0 pages=512 stable=1\n"));
  CHECK (CepheusSmemMapDriverEntry ((VOID *)1, NULL) == EFI_ALREADY_STARTED && Handles == 1);
  CHECK (!CepheusSmemMapDriverUnload ((VOID *)1));
  /* Emit exactly one synthetic complete transcript for the Python parser. */
  fputs (Log, stdout);
  for (unsigned Mode = 0; Mode < 5; Mode++) {
    DriverFixture ();
    switch (Mode) {
      case 0: Loaded.LoadOptions = NULL; break;
      case 1: Loaded.LoadOptionsSize--; break;
      case 2: Loaded.LoadOptionsSize += 2; break;
      case 3: Loaded.LoadOptions = (VOID *)L"cephsmemmap=0"; break;
      default: Loaded.LoadOptions = (VOID *)L"cephsmemmap=2"; break;
    }
    CHECK (CepheusSmemMapDriverEntry ((VOID *)1, NULL) == EFI_ACCESS_DENIED);
    CHECK (!Creates && !Pars && !DebugLines && !mSmemDriverAttempted);
  }
  DriverFixture (); HandleStatus = EFI_NOT_FOUND;
  CHECK (CepheusSmemMapDriverEntry ((VOID *)1, NULL) == EFI_NOT_FOUND && !Creates);
  DriverFixture (); Map[1] = Profile;
  CHECK (CepheusSmemMapDriverEntry ((VOID *)1, NULL) == EFI_INVALID_PARAMETER && !Creates);
  DriverFixture (); for (UINTN I = 0; I < MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT; I++) {
    Map[I] = Profile; Map[I].Name[0] = 'X';
  }
  CHECK (CepheusSmemMapDriverEntry ((VOID *)1, NULL) == EFI_INVALID_PARAMETER && !Creates);
  DriverFixture (); Map[0].Length = 0;
  CHECK (CepheusSmemMapDriverEntry ((VOID *)1, NULL) == EFI_INVALID_PARAMETER && !Creates);
  DriverFixture (); FaultPage = 5; ParBits = 1;
  CHECK (CepheusSmemMapDriverEntry ((VOID *)1, NULL) == EFI_NO_MAPPING && Closes == 1);
  CHECK (mSmemEvidence.Pages == 5 && DebugLines == 11);
  DriverFixture (); CloseStatus = EFI_DEVICE_ERROR;
  CHECK (!CepheusSmemMapDriverEntry ((VOID *)1, NULL) && mSmemExitEvent);
  CHECK (CepheusSmemMapDriverUnload ((VOID *)1) == EFI_DEVICE_ERROR);
  CloseStatus = EFI_SUCCESS; CHECK (!CepheusSmemMapDriverUnload ((VOID *)1) && !mSmemExitEvent);
  DriverFixture (); ExitAt = 1;
  CHECK (!CepheusSmemMapDriverEntry ((VOID *)1, NULL));
  CHECK (CepheusSmemMapDriverUnload ((VOID *)1) == EFI_ACCESS_DENIED && !Closes && !DebugLines);
  fprintf (stderr, "SMEM map driver: exact opt-in, bounded platform selection, partial logs, cleanup/unload PASS; mock firmware\n");
  return 0;
}
