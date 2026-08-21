add_executable(framebufferd src/main.m)

target_include_directories(framebufferd PRIVATE
  ${CMAKE_SOURCE_DIR}/shims/drm/drm/include
)

target_link_libraries(framebufferd PRIVATE
  displaysurface
  symrez
  -lobjc
  "-framework Foundation"
  "-framework QuartzCore"
  "-framework IOSurface"
  "-framework IOKit"
  "-framework CoreGraphics"
  "-framework CoreVideo"
)

# CoreBedtime ships entitlements.plist for framebufferd (iomfb windowserver
# profile, displayservice, …). Sign with it so DispDrvInit can own the panel.
add_custom_command(TARGET framebufferd POST_BUILD
  COMMAND codesign -s - --force --entitlements
          ${CMAKE_CURRENT_SOURCE_DIR}/entitlements.plist
          $<TARGET_FILE:framebufferd>
  COMMENT "Adhoc codesigning framebufferd with Mode B entitlements"
)

