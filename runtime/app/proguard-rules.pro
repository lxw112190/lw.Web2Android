# The entry point is referenced by a dynamically generated manifest in final APKs.
-keep class com.lw.web2android.runtime.** { *; }

# Keep source and line information in CI artifacts for useful crash diagnostics.
-keepattributes SourceFile,LineNumberTable

