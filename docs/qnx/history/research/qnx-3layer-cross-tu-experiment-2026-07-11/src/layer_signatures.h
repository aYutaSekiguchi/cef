// layer_signatures.h
// cross-TU リンク用の関数宣言 (RTTI 不要)。
#pragma once
namespace throw_qnx {
void AThrowsEdeadlkOnRecursiveLock();
void BSimple();
void BWithDtors();
}  // namespace throw_qnx
