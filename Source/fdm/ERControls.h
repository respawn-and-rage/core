#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

struct FERControlBinding
{
    FKey DefaultKey;
    const TCHAR* Label;
};

// The original keys identify the shipped mappings, including their alternate
// bindings. Remapping preserves the input actions, modifiers and triggers.
class FERControls
{
public:
    static TConstArrayView<FERControlBinding> Bindings();
    static FKey SlotKey(int32 Slot);
    static FString ShortLabel(const FKey& Key);
    FKey Key(const FKey& DefaultKey) const;
    bool Rebind(const FKey& DefaultKey, const FKey& NewKey);
    void Load();
    void Reset();

private:
    void Save() const;
    TMap<FKey, FKey> Overrides;
};
