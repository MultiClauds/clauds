// swift-tools-version: 5.10
import PackageDescription

let package = Package(
    name: "Clauds",
    platforms: [.macOS(.v14)],
    dependencies: [
        // Self-update. Each update must have a valid EdDSA signature (see docs/RELEASING.md).
        .package(url: "https://github.com/sparkle-project/Sparkle", from: "2.6.0")
    ],
    targets: [
        .executableTarget(
            name: "Clauds",
            dependencies: [.product(name: "Sparkle", package: "Sparkle")],
            path: "Sources/Clauds",
            linkerSettings: [
                // Sparkle.framework is in "Clauds.app/Contents/Frameworks".
                .unsafeFlags(["-Xlinker", "-rpath", "-Xlinker", "@executable_path/../Frameworks",
                              // For the binary in the build folder (the build runs it to draw the icons).
                              "-Xlinker", "-rpath", "-Xlinker", "@loader_path"])
            ]
        )
    ]
)
