function(xutils_regression target category)
    add_executable(${target} "${target}.c")
    target_link_libraries(${target} PRIVATE xutils)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
    foreach(test_case IN LISTS ARGN)
        add_test(NAME "${target}.${test_case}" COMMAND ${target} "${test_case}")
        set_tests_properties("${target}.${test_case}" PROPERTIES
            TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "${category};${target}")
    endforeach()
endfunction()

# Out-of-memory sweep. The linker wrap options are what let the test fail a
# chosen allocation, so this target is built by hand rather than through
# xutils_regression(), and only where the toolchain supports --wrap.
if(UNIX AND NOT APPLE)
    add_executable(alloc_fail_regression alloc_fail_regression.c)
    target_link_libraries(alloc_fail_regression PRIVATE xutils)
    target_link_options(alloc_fail_regression PRIVATE
        -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc
        -Wl,--wrap=free -Wl,--wrap=strdup)
    if(NOT MSVC)
        target_compile_options(alloc_fail_regression PRIVATE -Wall -Wextra)
    endif()
    foreach(alloc_case accounting sweep)
        add_test(NAME "alloc_fail_regression.${alloc_case}"
            COMMAND alloc_fail_regression "${alloc_case}")
        set_tests_properties("alloc_fail_regression.${alloc_case}" PROPERTIES
            TIMEOUT 300 SKIP_RETURN_CODE 77 LABELS "memory;alloc_fail_regression")
    endforeach()
endif()

xutils_regression(jwt_regression data roundtrip wrong_secret tampering boundaries malformed
    algorithms segments rs256)

# rs256 generates RSA keys, so how long it takes is probabilistic in the same
# way the rsa_regression cases are. The limit is there for the tail, not the
# mean: under valgrind on a shared runner the search can run long.
set_tests_properties(jwt_regression.rs256 PROPERTIES TIMEOUT 300)

if(UNIX)
    add_executable(api_regression api_regression.c)
    target_link_libraries(api_regression PRIVATE xutils)
    add_test(NAME api_regression COMMAND api_regression)
    set_tests_properties(api_regression PROPERTIES TIMEOUT 120)
endif()

# The shipped examples are the library's documented usage. They are built and
# run here so a change that breaks one is caught by ctest rather than by a
# reader: several of them verify their own output, and under the sanitizer and
# memcheck modes every one of them becomes a memory regression test too.
set(XUTILS_EXAMPLE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../examples")

function(xutils_example target)
    add_executable(example_${target} "${XUTILS_EXAMPLE_DIR}/${target}.c")
    target_link_libraries(example_${target} PRIVATE xutils)
    if(NOT MSVC)
        # The examples log through non-literal format strings on purpose.
        target_compile_options(example_${target} PRIVATE -Wall -Wno-format-security)
    endif()

    set(example_workdir "${CMAKE_CURRENT_BINARY_DIR}/example-run/${target}")
    file(MAKE_DIRECTORY "${example_workdir}")

    add_test(NAME "example.${target}" COMMAND example_${target} ${ARGN})
    set_tests_properties("example.${target}" PROPERTIES
        TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "example"
        WORKING_DIRECTORY "${example_workdir}")
endfunction()

xutils_example(strings)
xutils_example(array)
xutils_example(list)
xutils_example(map)
xutils_example(files)
xutils_example(thread)
xutils_example(xlog)
xutils_example(json "${XUTILS_EXAMPLE_DIR}/example.json")
# The remaining examples reach the network (statcov fetches a live site, the
# client and server pairs need a peer), so they are built by examples/ but not
# run here: the suite stays hermetic.

add_executable(addr_regression addr_regression.c)
target_link_libraries(addr_regression PRIVATE xutils)
add_test(NAME addr_regression COMMAND addr_regression)
set_tests_properties(addr_regression PROPERTIES TIMEOUT 120)

xutils_regression(addr_url_regression net components scheme_case default_ports percent_decoding
    parse_guards overlong unix_socket interfaces local_ip url_shapes)

xutils_regression(event_regression net lifecycle modify callback_delete timers wakeup status_strings
    capacity lookup event_fd service_guards timer_lifecycle interrupted write_byte)
xutils_regression(ws_regression net lengths partial coalesced invalid_wire creation_guards frame_types
    masking control_frames extra_data allocation)
xutils_regression(http_regression net partial pipeline headers basic_auth long_basic_auth reuse empty_body
    code_strings content_length start_line assemble header_lookup limits callbacks lifecycle unix_and_auth)

xutils_regression(map_regression data lifecycle reference_model operations iteration growth hashing
    pair_transfer)
xutils_regression(array_regression data lifecycle reference_model search_boundaries sorting mutation
    fixed_capacity search_agreement heap_array release_guards)
xutils_regression(json_regression data
    parse_matrix parse_boundaries builders_and_writers generated_string_edges byte_boundaries error_states pool_and_stress root_array_and_api_boundaries)
xutils_regression(json_limits_regression data boundaries)
xutils_regression(str_buf_regression data boundaries)
xutils_regression(base64_regression crypt boundaries)
if(OPENSSL_FOUND)
    xutils_regression(digest_regression crypt boundaries sha256_raw_final hmac_variants)
endif()
if(UNIX)
    xutils_regression(http_client_regression net connect link_exchange easy_exchange perform
        easy_perform auth_and_status client_failures truncated_answer stream_callback buffered_stream stopped_stream
    terminated_stream content_limit callback_guards length_stream length_buffered
    length_stopped length_terminated length_limit length_truncated)
    xutils_regression(sock_ext_regression net unix_stream chunked_transfer nonblocking_accept
        options buffers_and_alloc datagram_options address_accessors flag_guards)
    xutils_regression(sock_timeout_regression net boundaries)
    xutils_regression(sock_api_regression net address_conversion address_info status_strings
        accessors buffers listener create_guards udp)
endif()

xutils_regression(hash_regression data collisions ownership operations distribution pairs)
xutils_regression(list_regression data links rings stack_head push removal single_node insert_nodes ring_walk)
xutils_regression(api_io_regression net partial_io callback_disconnect peer_eof websocket_fragments unexpected_continuation
    buffered_upgrade eof_with_data)
xutils_regression(api_http_regression net http_partial http_pipeline auth_key auth_basic auth_short)
if(UNIX)
    xutils_regression(api_ctl_regression net vocabulary init listen_connect endpoint_guards
        timers workers buffers)
    xutils_regression(api_proto_regression net websocket mdtp http_endpoint timers
        status_strings workers worker_affinity)
    xutils_regression(api_events_regression net peer_closed peer_hunged user_callback
        interrupted_service tick extend_timer session_guards rx_size http_garbage
        http_oversized mdtp_garbage invalid_role inactive_role endpoint_guards ws_garbage_request
        ws_oversized_request ws_bad_response ws_upgrade_tokens)
endif()
xutils_regression(buf_regression data aliasing borrowed pointer_ownership ring_model growth edit
    accessors ownership_transfer heap_buffer data_buffer ring_boundaries
    string_and_stdin read_stdin)
xutils_regression(str_regression data capacities dynamic split_replace)
xutils_regression(xstring_regression data build edit transform substrings tokens construction colors)
xutils_regression(str_ops_regression data split match compare search replace cut tokens
    casing fill_and_trim duplicate concat colors random glob field_formatting append_remaining ansi_widths)
xutils_regression(aes_regression crypt ecb_vector modes siv_tampering generated_iv malformed_ciphertext)
if(OPENSSL_FOUND AND OPENSSL_VERSION VERSION_GREATER_EQUAL "3.0")
    xutils_regression(aes_siv_regression crypt openssl_vectors)
endif()
xutils_regression(pool_regression sys alignment_growth reallocation overflow accounting exhaustion heap_pool)
xutils_regression(thread_regression sys contention recursive invalid_start)
xutils_regression(sync_regression sys mutex recursive rwlock barrier task sleep atomics)
xutils_regression(time_regression sys calendar serialization http_year formats parse_guards
    conversions differences normalize clock fraction_bounds)
xutils_regression(crypt_regression crypt hex transforms dispatch cipher_names keyless_roundtrip
    keyed_roundtrip digests casear multy hex_columns)
if(OPENSSL_FOUND)
    xutils_regression(rsa_regression crypt signatures encryption invalid_keys key_sizes cross_key
        key_import key_files context size_limits build_support)

    # RSA key generation searches for primes, so how long it takes is
    # probabilistic and on a shared runner the tail is long. These cases are
    # fast in the common case; the limit is there for the tail, not the mean.
    foreach(rsa_case signatures encryption invalid_keys key_sizes cross_key
            key_import key_files context size_limits build_support)
        set_tests_properties("rsa_regression.${rsa_case}" PROPERTIES TIMEOUT 300)
    endforeach()
endif()
if(UNIX)
    xutils_regression(xfs_regression sys copy permissions directories nonregular_source copy_failures)
    xutils_regression(xfs_ops_regression sys file_io handles lines load paths directories ownership path_bounds)
    xutils_regression(srch_regression sys deep_tree name_matching recursion criteria text_search callback entries guards text_shapes)
    xutils_regression(cli_regression sys window_size window_lines window_align window_display
        progress_bounds progress_output progress_animation input terminal_modes terminal_input)
    xutils_regression(sig_regression sys register exit_handler errexit backtrace daemonize)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        xutils_regression(cpu_regression sys count affinity guards)
        xutils_regression(mon_regression sys lifecycle sampling restart target_pid)
    endif()
    xutils_regression(worker_sock_regression net listener_inheritance)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME worker_sock_regression.unix_backlog COMMAND worker_sock_regression unix_backlog)
        set_tests_properties(worker_sock_regression.unix_backlog PROPERTIES LABELS "net;worker_sock_regression" TIMEOUT 15)
    endif()
endif()

if(UNIX)
    xutils_regression(ntp_regression net request_reply no_reply zero_epoch guards)
endif()
xutils_regression(rtp_regression net header_fields header_guards csrc packet_blocks packet_guards assemble timestamp)
xutils_regression(mdtp_regression net type_mapping assemble_parse parse_guards payload_size_lies
    header_fields create_guards lifecycle hostile_header parse_into_stale
    header_combinations header_refusals)
xutils_regression(sock_regression net retry_read retry_write empty_and_eof)
xutils_regression(log_regression sys filtering large_message severities flag_mask config
    formatting file_output rotation throw callback_veto)
xutils_regression(type_regression sys float_bits concurrent printable byte_units kb_units version)

if(XUTILS_BUILD_FUZZERS)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "The parser fuzzer requires Clang")
    endif()
    add_executable(fuzz_parsers fuzz_parsers.c)
    target_link_libraries(fuzz_parsers PRIVATE xutils)
    target_link_options(fuzz_parsers PRIVATE -fsanitize=fuzzer,address,undefined)
    # Seeds are generated from source instead of being checked in, so the fuzz
    # run has a corpus on a clean tree and the selector bytes stay in sync.
    add_executable(fuzz_corpus fuzz_corpus.c)
    target_link_libraries(fuzz_corpus PRIVATE xutils)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    xutils_regression(allocation_regression allocation json_cleanup http_header buffer_preservation endpoint_ownership)
    target_link_options(allocation_regression PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
endif()

set_tests_properties(api_io_regression.partial_io PROPERTIES TIMEOUT 120)
if(OPENSSL_FOUND AND OPENSSL_VERSION VERSION_GREATER_EQUAL "1.1.0")
    xutils_regression(tls_regression net pending_record wrong_hostname untrusted)
    if(UNIX)
        xutils_regression(sock_tls_regression net handshake pkcs12_identity pkcs12_ownership untrusted cert_guards
            peer_endings closed_session_io status_strings write_after_reset ssl_teardown)
        xutils_regression(api_tls_regression net handshake pkcs12_listener partial_writes
            plaintext_client missing_identity)

        # Every case here runs a real handshake and moves bytes through it, so
        # under valgrind they are an order of magnitude slower than the rest.
        foreach(tls_case handshake pkcs12_listener partial_writes plaintext_client missing_identity)
            set_tests_properties("api_tls_regression.${tls_case}" PROPERTIES TIMEOUT 300)
        endforeach()

        foreach(sock_tls_case handshake pkcs12_identity pkcs12_ownership untrusted cert_guards
                peer_endings closed_session_io status_strings write_after_reset ssl_teardown)
            set_tests_properties("sock_tls_regression.${sock_tls_case}" PROPERTIES TIMEOUT 300)
        endforeach()
    endif()
endif()
