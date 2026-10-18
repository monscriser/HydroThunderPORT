add_custom_target(generate-hydrothunder
    COMMAND $<TARGET_FILE:rex::rexglue> codegen
            ${HT_CODEGEN_ARGS}
            "${CMAKE_CURRENT_BINARY_DIR}/manifests/hydrothunder.toml"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Generating recompiled code for default.xex"
    VERBATIM
)

add_custom_target(generate-all
    DEPENDS generate-hydrothunder
)

add_custom_target(ht_codegen DEPENDS generate-hydrothunder)
add_custom_target(all_codegen DEPENDS generate-all)
