#include "ERNetDriverEOS.h"
#include "Misc/CoreMisc.h"

bool UERNetDriverEOS::InitListen(FNetworkNotify* InNotify, FURL& URL, bool Reuse, FString& Error)
{
    // Dedicated servers have no EOS user and therefore no P2P address: EOS
    // advertises their public IP, so they always listen on IP.
    if (!URL.HasOption(TEXT("EROnline=1")) || IsRunningDedicatedServer()) URL.AddOption(TEXT("bUseIPSockets"));
    else if (!Super::IsAvailable()) { Error = TEXT("EOS transport unavailable."); return false; }
    UE_LOG(LogTemp, Display, TEXT("ER_TRANSPORT %s"), URL.HasOption(TEXT("bUseIPSockets")) ? TEXT("LAN/IP") : TEXT("EOS/P2P"));
    return Super::InitListen(InNotify, URL, Reuse, Error);
}
