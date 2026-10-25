# GameActivity

The Android host uses Google's [GameActivity](https://developer.android.com/games/agdk/game-activity),
version **4.4.2**, from the [official Google Maven artifact](https://dl.google.com/dl/android/maven2/androidx/games/games-activity/4.4.2/games-activity-4.4.2.aar).
SHA-256: `16069bb61a34e9cc7a4f6b4cef0ddb9e2f3f61fe781b282787f574f8880ba38e`.
The upstream license is Apache-2.0. No local source patches are applied.

The recipe installs the Prefab headers and static native archive for the selected ABI. The final application
links its own static C++ runtime. Java GameActivity/AppCompat dependencies belong to the Android application
package; they are not native engine dependencies.

The APK template separately pins AppCompat 1.7.1 and the Kotlin BOM 1.8.22 in Google Maven/Maven Central
(Apache-2.0), and Android Gradle Plugin 8.7.3 (Apache-2.0). GameActivity's published POM does not declare its
AppCompat superclass dependency. The Kotlin BOM aligns AppCompat's transitive standard-library variants.
