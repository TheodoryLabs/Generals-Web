// Closure externs for the Generals-Web release profile.
// Emscripten 6.x emits a runtime-guarded reference to Asyncify even when
// built with -sASYNCIFY=0; Closure's ADVANCED_OPTIMIZATIONS treats the bare
// identifier as an undeclared variable. Declaring it here keeps Closure
// honest without changing runtime behavior (the branch never executes).
// GeneralsX @build WebPort 2026-07-07
var Asyncify;
