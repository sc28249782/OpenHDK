// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "library/NativeStoreBinding.hpp"
#include <iostream>
namespace OpenHDK::AdmissionEvidence {
inline const char* callName(NativeAdmissionCall call) noexcept {
  switch(call) {
    case NativeAdmissionCall::Observe:return "identity-mount";
    case NativeAdmissionCall::Authority:return "epoch-check";
    case NativeAdmissionCall::Ancestor:return "ancestor-step";
    case NativeAdmissionCall::Terminate:return "ancestor-stop";
    case NativeAdmissionCall::Export:return "binding-export";
    case NativeAdmissionCall::Failure:return "failure";
  }
  return "unknown";
}
// Called after operations, with allocation prohibitions disabled. No pointers,
// username-bearing paths or authority tokens are published in these receipts.
inline bool trace(std::string_view tag,std::string_view prefix="ADMISSION") {
  const auto records=NativeStoreBindingTestAccess::records();
  std::cout<<prefix<<"_TRACE case="<<tag<<" count="<<records.size()<<" overflow="<<NativeStoreBindingTestAccess::overflow()<<'\n';
  for(const auto& r:records) {
    std::cout<<prefix<<"_RECORD case="<<tag<<" call="<<callName(r.call)<<" operation="<<static_cast<int>(r.operation)
        <<" dev="<<r.device<<" ino="<<r.inode<<" mount="<<r.mount<<" index=";
    if(r.index==SIZE_MAX)std::cout<<"none";else std::cout<<r.index;
    std::cout<<" errno="<<r.nativeError<<" owner_match="<<r.ownerMatch<<" held="<<r.held
        <<" epoch_match="<<r.epochMatch<<" error_code="<<r.errorCode<<" injected="<<r.injected<<'\n';
  }
  const bool okay=!NativeStoreBindingTestAccess::overflow();NativeStoreBindingTestAccess::receipts(false);return okay;
}
inline void begin() noexcept {NativeStoreBindingTestAccess::receipts();}
} // namespace OpenHDK::AdmissionEvidence
