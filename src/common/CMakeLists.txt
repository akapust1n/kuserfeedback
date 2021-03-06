set(common_src
    surveytargetexpression.cpp
    surveytargetexpressionevaluator.cpp
    surveytargetexpressionparser.cpp
)

if(HAVE_SURVEY_TARGET_EXPRESSIONS)
flex_target(surveytargetexpressionscanner
    surveytargetexpressionlexer.l
    ${CMAKE_CURRENT_BINARY_DIR}/surveytargetexpressionlexer.cpp
    COMPILE_FLAGS --header-file=${CMAKE_CURRENT_BINARY_DIR}/surveytargetexpressionscanner.h
)
if (CMAKE_VERSION VERSION_LESS 3.4.0)
    set(_bison_opts COMPILE_FLAGS --defines=${CMAKE_CURRENT_BINARY_DIR}/surveytargetexpressionparser_p.h)
else()
    set(_bison_opts DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/surveytargetexpressionparser_p.h)
endif()
bison_target(surveytargetexpressionparser
    surveytargetexpressionparser.y
    ${CMAKE_CURRENT_BINARY_DIR}/surveytargetexpressionparser_p.cpp
    ${_bison_opts}
)
add_flex_bison_dependency(surveytargetexpressionscanner surveytargetexpressionparser)
endif()

add_library(KUserFeedbackCommon STATIC
    ${common_src}
    ${BISON_surveytargetexpressionparser_OUTPUTS}
    ${FLEX_surveytargetexpressionscanner_OUTPUTS}
)

target_include_directories(KUserFeedbackCommon PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR};${CMAKE_CURRENT_SOURCE_DIR}/..>")
if(Qt5Core_FOUND)
    target_link_libraries(KUserFeedbackCommon LINK_PRIVATE Qt5::Core)
else()
    target_link_libraries(KUserFeedbackCommon LINK_PRIVATE ${QT_QTCORE_LIBS})
    set_target_properties(KUserFeedbackCommon PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
