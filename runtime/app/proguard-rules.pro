# The entry point is referenced by a dynamically generated manifest in final APKs.
# Keep only that stable boundary. Other runtime classes remain reachable through it,
# while generated R/BuildConfig classes can be removed by R8.
-keep public class com.lw.web2android.runtime.MainActivity { *; }
-keep public class com.lw.web2android.runtime.RuntimeFileProvider { *; }

# Keep source and line information in CI artifacts for useful crash diagnostics.
-keepattributes SourceFile,LineNumberTable
