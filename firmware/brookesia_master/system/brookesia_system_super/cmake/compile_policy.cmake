include_guard(GLOBAL)

# System Super keeps this wrapper for product-specific documentation and
# backwards-compatible BROOKESIA_SUPER_* cache variables.  The target scan and
# compile implementation live in brookesia_lib_utils so other examples and
# test apps can use the same policy.
function(brookesia_system_super_apply_compile_policy)
    if(NOT COMMAND brookesia_compile_tuning_apply_idf_components)
        message(FATAL_ERROR
            "brookesia_lib_utils compile tuning is unavailable; enable brookesia_lib_utils first")
    endif()
    brookesia_compile_tuning_apply_idf_components()
endfunction()
