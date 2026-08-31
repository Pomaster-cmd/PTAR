; ModuleID = '/mnt/data/PTAR_PROJECT_MASTER_v0_5_0/build/windows/ptar_edge_v04_clang_compute_probe.hlsl'
source_filename = "/mnt/data/PTAR_PROJECT_MASTER_v0_5_0/build/windows/ptar_edge_v04_clang_compute_probe.hlsl"
target datalayout = "e-m:e-p:32:32-i1:32-i8:8-i16:16-i32:32-i64:64-f16:16-f32:32-f64:64-n8:16:32:64"
target triple = "dxilv1.0-pc-shadermodel6.0-compute"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef float @"?min@@YAMMM@Z"(float noundef %0, float noundef %1) local_unnamed_addr #0 {
  %3 = fcmp olt float %0, %1
  %4 = select i1 %3, float %0, float %1
  ret float %4
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef float @"?max@@YAMMM@Z"(float noundef %0, float noundef %1) local_unnamed_addr #0 {
  %3 = fcmp ogt float %0, %1
  %4 = select i1 %3, float %0, float %1
  ret float %4
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?min@@YAT?$__vector@M$03@__clang@@T12@0@Z"(<4 x float> noundef %0, <4 x float> noundef %1) local_unnamed_addr #0 {
  %3 = tail call <4 x float> @llvm.minnum.v4f32(<4 x float> %0, <4 x float> %1)
  ret <4 x float> %3
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare <4 x float> @llvm.minnum.v4f32(<4 x float>, <4 x float>) #1

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?max@@YAT?$__vector@M$03@__clang@@T12@0@Z"(<4 x float> noundef %0, <4 x float> noundef %1) local_unnamed_addr #0 {
  %3 = tail call <4 x float> @llvm.maxnum.v4f32(<4 x float> %0, <4 x float> %1)
  ret <4 x float> %3
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare <4 x float> @llvm.maxnum.v4f32(<4 x float>, <4 x float>) #1

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef float @"?clamp@@YAMMMM@Z"(float noundef %0, float noundef %1, float noundef %2) local_unnamed_addr #0 {
  %4 = fcmp ogt float %0, %1
  %5 = select i1 %4, float %0, float %1
  %6 = fcmp olt float %5, %2
  %7 = select i1 %6, float %5, float %2
  ret float %7
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?clamp@@YAT?$__vector@M$03@__clang@@T12@00@Z"(<4 x float> noundef %0, <4 x float> noundef %1, <4 x float> noundef %2) local_unnamed_addr #0 {
  %4 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %0, <4 x float> %1)
  %5 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %4, <4 x float> %2)
  ret <4 x float> %5
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef float @"?saturate@@YAMM@Z"(float noundef %0) local_unnamed_addr #0 {
  %2 = fcmp ogt float %0, 0.000000e+00
  %3 = select i1 %2, float %0, float 0.000000e+00
  %4 = fcmp olt float %3, 1.000000e+00
  %5 = select i1 %4, float %3, float 1.000000e+00
  ret float %5
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?lerp@@YAT?$__vector@M$03@__clang@@T12@0M@Z"(<4 x float> noundef %0, <4 x float> noundef %1, float noundef %2) local_unnamed_addr #0 {
  %4 = insertelement <4 x float> poison, float %2, i64 0
  %5 = shufflevector <4 x float> %4, <4 x float> poison, <4 x i32> zeroinitializer
  %6 = fsub <4 x float> %1, %0
  %7 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %5, <4 x float> %6, <4 x float> %0)
  ret <4 x float> %7
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare <4 x float> @llvm.fmuladd.v4f32(<4 x float>, <4 x float>, <4 x float>) #1

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef float @"?rcp@@YAMM@Z"(float noundef %0) local_unnamed_addr #0 {
  %2 = fdiv float 1.000000e+00, %0
  ret float %2
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <2 x float> @"?PTARStepSmoothness@@YAT?$__vector@M$01@__clang@@MMMM@Z"(float noundef %0, float noundef %1, float noundef %2, float noundef %3) local_unnamed_addr #0 {
  %5 = fsub float %1, %0
  %6 = fsub float %2, %1
  %7 = fsub float %3, %2
  %8 = fsub float %6, %5
  %9 = fsub float %7, %6
  %10 = fmul float %8, 0x3FF1555560000000
  %11 = fmul float %8, %10
  %12 = tail call float @llvm.fmuladd.f32(float %6, float %6, float %11)
  %13 = insertelement <2 x float> poison, float %12, i64 0
  %14 = fmul float %9, 0x3FF1555560000000
  %15 = fmul float %9, %14
  %16 = tail call float @llvm.fmuladd.f32(float %6, float %6, float %15)
  %17 = insertelement <2 x float> %13, float %16, i64 1
  ret <2 x float> %17
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.fmuladd.f32(float, float, float) #1

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef float @"?PTARStepWeightP1@@YAMMMMMM@Z"(float noundef %0, float noundef %1, float noundef %2, float noundef %3, float noundef %4) local_unnamed_addr #0 {
  %6 = fadd float %0, %4
  %7 = fadd float %1, %4
  %8 = fmul float %7, %2
  %9 = fmul float %6, %3
  %10 = fadd float %8, %9
  %11 = fdiv float 1.000000e+00, %10
  %12 = fmul float %8, %11
  ret float %12
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef float @"?PTARStepWeightP2@@YAMMMMMM@Z"(float noundef %0, float noundef %1, float noundef %2, float noundef %3, float noundef %4) local_unnamed_addr #0 {
  %6 = fadd float %0, %4
  %7 = fadd float %1, %4
  %8 = fmul float %7, %2
  %9 = fmul float %7, %8
  %10 = fmul float %6, %3
  %11 = fmul float %6, %10
  %12 = fadd float %9, %11
  %13 = fdiv float 1.000000e+00, %12
  %14 = fmul float %9, %13
  ret float %14
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: write)
define void @"?PTARStepCandidates13@@YAXT?$__vector@M$03@__clang@@000AAT12@1@Z"(<4 x float> noundef %0, <4 x float> noundef %1, <4 x float> noundef %2, <4 x float> noundef %3, ptr nocapture noundef nonnull writeonly align 16 dereferenceable(16) %4, ptr nocapture noundef nonnull writeonly align 16 dereferenceable(16) %5) local_unnamed_addr #2 {
  %7 = fneg <4 x float> %0
  %8 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %1, <4 x float> <float 8.000000e+00, float 8.000000e+00, float 8.000000e+00, float 8.000000e+00>, <4 x float> %7)
  %9 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %2, <4 x float> <float 2.000000e+00, float 2.000000e+00, float 2.000000e+00, float 2.000000e+00>, <4 x float> %8)
  %10 = fmul <4 x float> %9, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  store <4 x float> %10, ptr %4, align 16, !tbaa !4
  %11 = fmul <4 x float> %2, <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>
  %12 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %1, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %11)
  %13 = fsub <4 x float> %12, %3
  %14 = fmul <4 x float> %13, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  store <4 x float> %14, ptr %5, align 16, !tbaa !4
  ret void
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: write)
define void @"?PTARStepCandidates23@@YAXT?$__vector@M$03@__clang@@000AAT12@1@Z"(<4 x float> noundef %0, <4 x float> noundef %1, <4 x float> noundef %2, <4 x float> noundef %3, ptr nocapture noundef nonnull writeonly align 16 dereferenceable(16) %4, ptr nocapture noundef nonnull writeonly align 16 dereferenceable(16) %5) local_unnamed_addr #2 {
  %7 = fneg <4 x float> %0
  %8 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %1, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %7)
  %9 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %2, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %8)
  %10 = fmul <4 x float> %9, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  store <4 x float> %10, ptr %4, align 16, !tbaa !4
  %11 = fmul <4 x float> %2, <float 8.000000e+00, float 8.000000e+00, float 8.000000e+00, float 8.000000e+00>
  %12 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %1, <4 x float> <float 2.000000e+00, float 2.000000e+00, float 2.000000e+00, float 2.000000e+00>, <4 x float> %11)
  %13 = fsub <4 x float> %12, %3
  %14 = fmul <4 x float> %13, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  store <4 x float> %14, ptr %5, align 16, !tbaa !4
  ret void
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?PTARStepBlend@@YAT?$__vector@M$03@__clang@@T12@0M@Z"(<4 x float> noundef %0, <4 x float> noundef %1, float noundef %2) local_unnamed_addr #0 {
  %4 = insertelement <4 x float> poison, float %2, i64 0
  %5 = shufflevector <4 x float> %4, <4 x float> poison, <4 x i32> zeroinitializer
  %6 = fsub <4 x float> %0, %1
  %7 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %5, <4 x float> %6, <4 x float> %1)
  ret <4 x float> %7
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?PTARStepClampCentral@@YAT?$__vector@M$03@__clang@@T12@00@Z"(<4 x float> noundef %0, <4 x float> noundef %1, <4 x float> noundef %2) local_unnamed_addr #0 {
  %4 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %1, <4 x float> %2)
  %5 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %1, <4 x float> %2)
  %6 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %0, <4 x float> %5)
  %7 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %6, <4 x float> %4)
  ret <4 x float> %7
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?PTARStepSW1_13@@YAT?$__vector@M$03@__clang@@T12@000MMMMM@Z"(<4 x float> noundef %0, <4 x float> noundef %1, <4 x float> noundef %2, <4 x float> noundef %3, float noundef %4, float noundef %5, float noundef %6, float noundef %7, float noundef %8) local_unnamed_addr #0 {
  %10 = fneg <4 x float> %0
  %11 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %1, <4 x float> <float 8.000000e+00, float 8.000000e+00, float 8.000000e+00, float 8.000000e+00>, <4 x float> %10)
  %12 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %2, <4 x float> <float 2.000000e+00, float 2.000000e+00, float 2.000000e+00, float 2.000000e+00>, <4 x float> %11)
  %13 = fmul <4 x float> %12, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %14 = fmul <4 x float> %2, <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>
  %15 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %1, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %14)
  %16 = fsub <4 x float> %15, %3
  %17 = fmul <4 x float> %16, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %18 = fsub float %5, %4
  %19 = fsub float %6, %5
  %20 = fsub float %7, %6
  %21 = fsub float %19, %18
  %22 = fsub float %20, %19
  %23 = fmul float %21, 0x3FF1555560000000
  %24 = fmul float %21, %23
  %25 = tail call float @llvm.fmuladd.f32(float %19, float %19, float %24)
  %26 = fmul float %22, 0x3FF1555560000000
  %27 = fmul float %22, %26
  %28 = tail call float @llvm.fmuladd.f32(float %19, float %19, float %27)
  %29 = fadd float %25, %8
  %30 = fadd float %28, %8
  %31 = fmul float %30, 0x3FE1C71C80000000
  %32 = fmul float %29, 0x3FDC71C720000000
  %33 = fadd float %32, %31
  %34 = fdiv float 1.000000e+00, %33
  %35 = fmul float %31, %34
  %36 = insertelement <4 x float> poison, float %35, i64 0
  %37 = shufflevector <4 x float> %36, <4 x float> poison, <4 x i32> zeroinitializer
  %38 = fsub <4 x float> %13, %17
  %39 = tail call noundef <4 x float> @llvm.fmuladd.v4f32(<4 x float> %37, <4 x float> %38, <4 x float> %17)
  ret <4 x float> %39
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?PTARStepSW1_23@@YAT?$__vector@M$03@__clang@@T12@000MMMMM@Z"(<4 x float> noundef %0, <4 x float> noundef %1, <4 x float> noundef %2, <4 x float> noundef %3, float noundef %4, float noundef %5, float noundef %6, float noundef %7, float noundef %8) local_unnamed_addr #0 {
  %10 = fneg <4 x float> %0
  %11 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %1, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %10)
  %12 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %2, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %11)
  %13 = fmul <4 x float> %12, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %14 = fmul <4 x float> %2, <float 8.000000e+00, float 8.000000e+00, float 8.000000e+00, float 8.000000e+00>
  %15 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %1, <4 x float> <float 2.000000e+00, float 2.000000e+00, float 2.000000e+00, float 2.000000e+00>, <4 x float> %14)
  %16 = fsub <4 x float> %15, %3
  %17 = fmul <4 x float> %16, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %18 = fsub float %5, %4
  %19 = fsub float %6, %5
  %20 = fsub float %7, %6
  %21 = fsub float %19, %18
  %22 = fsub float %20, %19
  %23 = fmul float %21, 0x3FF1555560000000
  %24 = fmul float %21, %23
  %25 = tail call float @llvm.fmuladd.f32(float %19, float %19, float %24)
  %26 = fmul float %22, 0x3FF1555560000000
  %27 = fmul float %22, %26
  %28 = tail call float @llvm.fmuladd.f32(float %19, float %19, float %27)
  %29 = fadd float %25, %8
  %30 = fadd float %28, %8
  %31 = fmul float %30, 0x3FDC71C720000000
  %32 = fmul float %29, 0x3FE1C71C80000000
  %33 = fadd float %32, %31
  %34 = fdiv float 1.000000e+00, %33
  %35 = fmul float %31, %34
  %36 = insertelement <4 x float> poison, float %35, i64 0
  %37 = shufflevector <4 x float> %36, <4 x float> poison, <4 x i32> zeroinitializer
  %38 = fsub <4 x float> %13, %17
  %39 = tail call noundef <4 x float> @llvm.fmuladd.v4f32(<4 x float> %37, <4 x float> %38, <4 x float> %17)
  ret <4 x float> %39
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?PTARStepSW2_13@@YAT?$__vector@M$03@__clang@@T12@000MMMMM@Z"(<4 x float> noundef %0, <4 x float> noundef %1, <4 x float> noundef %2, <4 x float> noundef %3, float noundef %4, float noundef %5, float noundef %6, float noundef %7, float noundef %8) local_unnamed_addr #0 {
  %10 = fneg <4 x float> %0
  %11 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %1, <4 x float> <float 8.000000e+00, float 8.000000e+00, float 8.000000e+00, float 8.000000e+00>, <4 x float> %10)
  %12 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %2, <4 x float> <float 2.000000e+00, float 2.000000e+00, float 2.000000e+00, float 2.000000e+00>, <4 x float> %11)
  %13 = fmul <4 x float> %12, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %14 = fmul <4 x float> %2, <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>
  %15 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %1, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %14)
  %16 = fsub <4 x float> %15, %3
  %17 = fmul <4 x float> %16, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %18 = fsub float %5, %4
  %19 = fsub float %6, %5
  %20 = fsub float %7, %6
  %21 = fsub float %19, %18
  %22 = fsub float %20, %19
  %23 = fmul float %21, 0x3FF1555560000000
  %24 = fmul float %21, %23
  %25 = tail call float @llvm.fmuladd.f32(float %19, float %19, float %24)
  %26 = fmul float %22, 0x3FF1555560000000
  %27 = fmul float %22, %26
  %28 = tail call float @llvm.fmuladd.f32(float %19, float %19, float %27)
  %29 = fadd float %25, %8
  %30 = fadd float %28, %8
  %31 = fmul float %30, 0x3FE1C71C80000000
  %32 = fmul float %30, %31
  %33 = fmul float %29, 0x3FDC71C720000000
  %34 = fmul float %29, %33
  %35 = fadd float %34, %32
  %36 = fdiv float 1.000000e+00, %35
  %37 = fmul float %32, %36
  %38 = insertelement <4 x float> poison, float %37, i64 0
  %39 = shufflevector <4 x float> %38, <4 x float> poison, <4 x i32> zeroinitializer
  %40 = fsub <4 x float> %13, %17
  %41 = tail call noundef <4 x float> @llvm.fmuladd.v4f32(<4 x float> %39, <4 x float> %40, <4 x float> %17)
  %42 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %1, <4 x float> %2)
  %43 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %1, <4 x float> %2)
  %44 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %41, <4 x float> %43)
  %45 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %44, <4 x float> %42)
  ret <4 x float> %45
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?PTARStepSW2_23@@YAT?$__vector@M$03@__clang@@T12@000MMMMM@Z"(<4 x float> noundef %0, <4 x float> noundef %1, <4 x float> noundef %2, <4 x float> noundef %3, float noundef %4, float noundef %5, float noundef %6, float noundef %7, float noundef %8) local_unnamed_addr #0 {
  %10 = fneg <4 x float> %0
  %11 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %1, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %10)
  %12 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %2, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %11)
  %13 = fmul <4 x float> %12, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %14 = fmul <4 x float> %2, <float 8.000000e+00, float 8.000000e+00, float 8.000000e+00, float 8.000000e+00>
  %15 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %1, <4 x float> <float 2.000000e+00, float 2.000000e+00, float 2.000000e+00, float 2.000000e+00>, <4 x float> %14)
  %16 = fsub <4 x float> %15, %3
  %17 = fmul <4 x float> %16, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %18 = fsub float %5, %4
  %19 = fsub float %6, %5
  %20 = fsub float %7, %6
  %21 = fsub float %19, %18
  %22 = fsub float %20, %19
  %23 = fmul float %21, 0x3FF1555560000000
  %24 = fmul float %21, %23
  %25 = tail call float @llvm.fmuladd.f32(float %19, float %19, float %24)
  %26 = fmul float %22, 0x3FF1555560000000
  %27 = fmul float %22, %26
  %28 = tail call float @llvm.fmuladd.f32(float %19, float %19, float %27)
  %29 = fadd float %25, %8
  %30 = fadd float %28, %8
  %31 = fmul float %30, 0x3FDC71C720000000
  %32 = fmul float %30, %31
  %33 = fmul float %29, 0x3FE1C71C80000000
  %34 = fmul float %29, %33
  %35 = fadd float %34, %32
  %36 = fdiv float 1.000000e+00, %35
  %37 = fmul float %32, %36
  %38 = insertelement <4 x float> poison, float %37, i64 0
  %39 = shufflevector <4 x float> %38, <4 x float> poison, <4 x i32> zeroinitializer
  %40 = fsub <4 x float> %13, %17
  %41 = tail call noundef <4 x float> @llvm.fmuladd.v4f32(<4 x float> %39, <4 x float> %40, <4 x float> %17)
  %42 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %1, <4 x float> %2)
  %43 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %1, <4 x float> %2)
  %44 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %41, <4 x float> %43)
  %45 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %44, <4 x float> %42)
  ret <4 x float> %45
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?PTARStepSW3_13@@YAT?$__vector@M$03@__clang@@T12@M0000MMMMM@Z"(<4 x float> noundef %0, float noundef %1, <4 x float> noundef %2, <4 x float> noundef %3, <4 x float> noundef %4, <4 x float> noundef %5, float noundef %6, float noundef %7, float noundef %8, float noundef %9, float noundef %10) local_unnamed_addr #0 {
  %12 = fneg <4 x float> %2
  %13 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %3, <4 x float> <float 8.000000e+00, float 8.000000e+00, float 8.000000e+00, float 8.000000e+00>, <4 x float> %12)
  %14 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %4, <4 x float> <float 2.000000e+00, float 2.000000e+00, float 2.000000e+00, float 2.000000e+00>, <4 x float> %13)
  %15 = fmul <4 x float> %14, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %16 = fmul <4 x float> %4, <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>
  %17 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %3, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %16)
  %18 = fsub <4 x float> %17, %5
  %19 = fmul <4 x float> %18, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %20 = fsub float %7, %6
  %21 = fsub float %8, %7
  %22 = fsub float %9, %8
  %23 = fsub float %21, %20
  %24 = fsub float %22, %21
  %25 = fmul float %23, 0x3FF1555560000000
  %26 = fmul float %23, %25
  %27 = tail call float @llvm.fmuladd.f32(float %21, float %21, float %26)
  %28 = fmul float %24, 0x3FF1555560000000
  %29 = fmul float %24, %28
  %30 = tail call float @llvm.fmuladd.f32(float %21, float %21, float %29)
  %31 = fadd float %27, %10
  %32 = fadd float %30, %10
  %33 = fmul float %32, 0x3FE1C71C80000000
  %34 = fmul float %32, %33
  %35 = fmul float %31, 0x3FDC71C720000000
  %36 = fmul float %31, %35
  %37 = fadd float %36, %34
  %38 = fdiv float 1.000000e+00, %37
  %39 = fmul float %34, %38
  %40 = insertelement <4 x float> poison, float %39, i64 0
  %41 = shufflevector <4 x float> %40, <4 x float> poison, <4 x i32> zeroinitializer
  %42 = fsub <4 x float> %15, %19
  %43 = tail call noundef <4 x float> @llvm.fmuladd.v4f32(<4 x float> %41, <4 x float> %42, <4 x float> %19)
  %44 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %3, <4 x float> %4)
  %45 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %3, <4 x float> %4)
  %46 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %43, <4 x float> %45)
  %47 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %46, <4 x float> %44)
  %48 = fcmp ogt float %1, 0.000000e+00
  %49 = select i1 %48, float %1, float 0.000000e+00
  %50 = fcmp olt float %49, 1.000000e+00
  %51 = select i1 %50, float %49, float 1.000000e+00
  %52 = insertelement <4 x float> poison, float %51, i64 0
  %53 = shufflevector <4 x float> %52, <4 x float> poison, <4 x i32> zeroinitializer
  %54 = fsub <4 x float> %47, %0
  %55 = tail call noundef <4 x float> @llvm.fmuladd.v4f32(<4 x float> %53, <4 x float> %54, <4 x float> %0)
  ret <4 x float> %55
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?PTARStepSW3_23@@YAT?$__vector@M$03@__clang@@T12@M0000MMMMM@Z"(<4 x float> noundef %0, float noundef %1, <4 x float> noundef %2, <4 x float> noundef %3, <4 x float> noundef %4, <4 x float> noundef %5, float noundef %6, float noundef %7, float noundef %8, float noundef %9, float noundef %10) local_unnamed_addr #0 {
  %12 = fneg <4 x float> %2
  %13 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %3, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %12)
  %14 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %4, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %13)
  %15 = fmul <4 x float> %14, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %16 = fmul <4 x float> %4, <float 8.000000e+00, float 8.000000e+00, float 8.000000e+00, float 8.000000e+00>
  %17 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %3, <4 x float> <float 2.000000e+00, float 2.000000e+00, float 2.000000e+00, float 2.000000e+00>, <4 x float> %16)
  %18 = fsub <4 x float> %17, %5
  %19 = fmul <4 x float> %18, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %20 = fsub float %7, %6
  %21 = fsub float %8, %7
  %22 = fsub float %9, %8
  %23 = fsub float %21, %20
  %24 = fsub float %22, %21
  %25 = fmul float %23, 0x3FF1555560000000
  %26 = fmul float %23, %25
  %27 = tail call float @llvm.fmuladd.f32(float %21, float %21, float %26)
  %28 = fmul float %24, 0x3FF1555560000000
  %29 = fmul float %24, %28
  %30 = tail call float @llvm.fmuladd.f32(float %21, float %21, float %29)
  %31 = fadd float %27, %10
  %32 = fadd float %30, %10
  %33 = fmul float %32, 0x3FDC71C720000000
  %34 = fmul float %32, %33
  %35 = fmul float %31, 0x3FE1C71C80000000
  %36 = fmul float %31, %35
  %37 = fadd float %36, %34
  %38 = fdiv float 1.000000e+00, %37
  %39 = fmul float %34, %38
  %40 = insertelement <4 x float> poison, float %39, i64 0
  %41 = shufflevector <4 x float> %40, <4 x float> poison, <4 x i32> zeroinitializer
  %42 = fsub <4 x float> %15, %19
  %43 = tail call noundef <4 x float> @llvm.fmuladd.v4f32(<4 x float> %41, <4 x float> %42, <4 x float> %19)
  %44 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %3, <4 x float> %4)
  %45 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %3, <4 x float> %4)
  %46 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %43, <4 x float> %45)
  %47 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %46, <4 x float> %44)
  %48 = fcmp ogt float %1, 0.000000e+00
  %49 = select i1 %48, float %1, float 0.000000e+00
  %50 = fcmp olt float %49, 1.000000e+00
  %51 = select i1 %50, float %49, float 1.000000e+00
  %52 = insertelement <4 x float> poison, float %51, i64 0
  %53 = shufflevector <4 x float> %52, <4 x float> poison, <4 x i32> zeroinitializer
  %54 = fsub <4 x float> %47, %0
  %55 = tail call noundef <4 x float> @llvm.fmuladd.v4f32(<4 x float> %53, <4 x float> %54, <4 x float> %0)
  ret <4 x float> %55
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?PTAREdgeV04Phase13@@YAT?$__vector@M$03@__clang@@T12@M0000MMMMM@Z"(<4 x float> noundef %0, float noundef %1, <4 x float> noundef %2, <4 x float> noundef %3, <4 x float> noundef %4, <4 x float> noundef %5, float noundef %6, float noundef %7, float noundef %8, float noundef %9, float noundef %10) local_unnamed_addr #0 {
  %12 = fneg <4 x float> %2
  %13 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %3, <4 x float> <float 8.000000e+00, float 8.000000e+00, float 8.000000e+00, float 8.000000e+00>, <4 x float> %12)
  %14 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %4, <4 x float> <float 2.000000e+00, float 2.000000e+00, float 2.000000e+00, float 2.000000e+00>, <4 x float> %13)
  %15 = fmul <4 x float> %14, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %16 = fmul <4 x float> %4, <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>
  %17 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %3, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %16)
  %18 = fsub <4 x float> %17, %5
  %19 = fmul <4 x float> %18, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %20 = fsub float %7, %6
  %21 = fsub float %8, %7
  %22 = fsub float %9, %8
  %23 = fsub float %21, %20
  %24 = fsub float %22, %21
  %25 = fmul float %23, 0x3FF1555560000000
  %26 = fmul float %23, %25
  %27 = tail call float @llvm.fmuladd.f32(float %21, float %21, float %26)
  %28 = fmul float %24, 0x3FF1555560000000
  %29 = fmul float %24, %28
  %30 = tail call float @llvm.fmuladd.f32(float %21, float %21, float %29)
  %31 = fadd float %27, %10
  %32 = fadd float %30, %10
  %33 = fmul float %32, 0x3FE1C71C80000000
  %34 = fmul float %32, %33
  %35 = fmul float %31, 0x3FDC71C720000000
  %36 = fmul float %31, %35
  %37 = fadd float %36, %34
  %38 = fdiv float 1.000000e+00, %37
  %39 = fmul float %34, %38
  %40 = insertelement <4 x float> poison, float %39, i64 0
  %41 = shufflevector <4 x float> %40, <4 x float> poison, <4 x i32> zeroinitializer
  %42 = fsub <4 x float> %15, %19
  %43 = tail call noundef <4 x float> @llvm.fmuladd.v4f32(<4 x float> %41, <4 x float> %42, <4 x float> %19)
  %44 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %3, <4 x float> %4)
  %45 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %3, <4 x float> %4)
  %46 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %43, <4 x float> %45)
  %47 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %46, <4 x float> %44)
  %48 = fcmp ogt float %1, 0.000000e+00
  %49 = select i1 %48, float %1, float 0.000000e+00
  %50 = fcmp olt float %49, 1.000000e+00
  %51 = select i1 %50, float %49, float 1.000000e+00
  %52 = insertelement <4 x float> poison, float %51, i64 0
  %53 = shufflevector <4 x float> %52, <4 x float> poison, <4 x i32> zeroinitializer
  %54 = fsub <4 x float> %47, %0
  %55 = tail call noundef <4 x float> @llvm.fmuladd.v4f32(<4 x float> %53, <4 x float> %54, <4 x float> %0)
  ret <4 x float> %55
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define noundef <4 x float> @"?PTAREdgeV04Phase23@@YAT?$__vector@M$03@__clang@@T12@M0000MMMMM@Z"(<4 x float> noundef %0, float noundef %1, <4 x float> noundef %2, <4 x float> noundef %3, <4 x float> noundef %4, <4 x float> noundef %5, float noundef %6, float noundef %7, float noundef %8, float noundef %9, float noundef %10) local_unnamed_addr #0 {
  %12 = fneg <4 x float> %2
  %13 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %3, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %12)
  %14 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %4, <4 x float> <float 5.000000e+00, float 5.000000e+00, float 5.000000e+00, float 5.000000e+00>, <4 x float> %13)
  %15 = fmul <4 x float> %14, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %16 = fmul <4 x float> %4, <float 8.000000e+00, float 8.000000e+00, float 8.000000e+00, float 8.000000e+00>
  %17 = tail call <4 x float> @llvm.fmuladd.v4f32(<4 x float> %3, <4 x float> <float 2.000000e+00, float 2.000000e+00, float 2.000000e+00, float 2.000000e+00>, <4 x float> %16)
  %18 = fsub <4 x float> %17, %5
  %19 = fmul <4 x float> %18, <float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000, float 0x3FBC71C720000000>
  %20 = fsub float %7, %6
  %21 = fsub float %8, %7
  %22 = fsub float %9, %8
  %23 = fsub float %21, %20
  %24 = fsub float %22, %21
  %25 = fmul float %23, 0x3FF1555560000000
  %26 = fmul float %23, %25
  %27 = tail call float @llvm.fmuladd.f32(float %21, float %21, float %26)
  %28 = fmul float %24, 0x3FF1555560000000
  %29 = fmul float %24, %28
  %30 = tail call float @llvm.fmuladd.f32(float %21, float %21, float %29)
  %31 = fadd float %27, %10
  %32 = fadd float %30, %10
  %33 = fmul float %32, 0x3FDC71C720000000
  %34 = fmul float %32, %33
  %35 = fmul float %31, 0x3FE1C71C80000000
  %36 = fmul float %31, %35
  %37 = fadd float %36, %34
  %38 = fdiv float 1.000000e+00, %37
  %39 = fmul float %34, %38
  %40 = insertelement <4 x float> poison, float %39, i64 0
  %41 = shufflevector <4 x float> %40, <4 x float> poison, <4 x i32> zeroinitializer
  %42 = fsub <4 x float> %15, %19
  %43 = tail call noundef <4 x float> @llvm.fmuladd.v4f32(<4 x float> %41, <4 x float> %42, <4 x float> %19)
  %44 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %3, <4 x float> %4)
  %45 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %3, <4 x float> %4)
  %46 = tail call noundef <4 x float> @llvm.maxnum.v4f32(<4 x float> %43, <4 x float> %45)
  %47 = tail call noundef <4 x float> @llvm.minnum.v4f32(<4 x float> %46, <4 x float> %44)
  %48 = fcmp ogt float %1, 0.000000e+00
  %49 = select i1 %48, float %1, float 0.000000e+00
  %50 = fcmp olt float %49, 1.000000e+00
  %51 = select i1 %50, float %49, float 1.000000e+00
  %52 = insertelement <4 x float> poison, float %51, i64 0
  %53 = shufflevector <4 x float> %52, <4 x float> poison, <4 x i32> zeroinitializer
  %54 = fsub <4 x float> %47, %0
  %55 = tail call noundef <4 x float> @llvm.fmuladd.v4f32(<4 x float> %53, <4 x float> %54, <4 x float> %0)
  ret <4 x float> %55
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none)
define void @main() local_unnamed_addr #3 {
  ret void
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #2 = { mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: write) "frame-pointer"="all" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }
attributes #3 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) "frame-pointer"="all" "hlsl.numthreads"="1,1,1" "hlsl.shader"="compute" "no-trapping-math"="true" "stack-protector-buffer-size"="8" }

!llvm.module.flags = !{!0, !1}
!dx.valver = !{!2}
!llvm.ident = !{!3}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 7, !"frame-pointer", i32 2}
!2 = !{i32 1, i32 7}
!3 = !{!"clang version 17.0.0 (https://github.com/swiftlang/llvm-project.git 10999b6d034fe318f3d56c83bddb6572593a8bb0)"}
!4 = !{!5, !5, i64 0}
!5 = !{!"omnipotent char", !6, i64 0}
!6 = !{!"Simple C++ TBAA"}
