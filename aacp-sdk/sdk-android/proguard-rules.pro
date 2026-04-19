# AACP SDK ProGuard rules

# Giữ public API class và methods — consumer có thể dùng reflection
-keep public class com.aacp.sdk.CarPlayManager { *; }
-keep public interface com.aacp.sdk.CarPlayManager$Listener { *; }
-keep public class com.aacp.sdk.media.** { *; }
-keep public class com.aacp.sdk.input.** { *; }

# Giữ JNI methods — tên phải match với jni_bridge.cpp
-keepclassmembers class com.aacp.sdk.CarPlayManager {
    private native <methods>;
}

# Giữ constants
-keepclassmembers class com.aacp.sdk.CarPlayManager {
    public static final int STATE_*;
    public static final int ERROR_*;
}
