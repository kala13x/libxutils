if(XUTILS_TEST_COVERAGE AND "${CMAKE_C_FLAGS}" MATCHES "--coverage")
    add_compile_definitions(XTEST_GCOV_DUMP)
endif()

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

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    xutils_regression(api_worker_fault_regression net start_failure wait_failures stop_failures watch_failures watch_shutdown
        wait_single watch_reaped start_guards start_retry)
    target_link_options(api_worker_fault_regression PRIVATE -Wl,--wrap=calloc -Wl,--wrap=fork -Wl,--wrap=kill -Wl,--wrap=waitpid)
    xutils_regression(worker_feature feature direct one two four eight shared unix restart_one restart_four churn
        repeated capacity tls_direct tls_one tls_four tls_shared tls_restart_one tls_restart_four tls_churn
        shutdown_direct shutdown_one shutdown_four tls_shutdown_one tls_shutdown_four)
    target_link_options(worker_feature PRIVATE -Wl,--wrap=waitpid)
    set_tests_properties(worker_feature.direct worker_feature.one worker_feature.two worker_feature.four
        worker_feature.eight worker_feature.shared worker_feature.unix worker_feature.restart_one
        worker_feature.restart_four worker_feature.churn worker_feature.tls_direct worker_feature.tls_one
        worker_feature.tls_four worker_feature.tls_shared worker_feature.tls_restart_one worker_feature.tls_restart_four
        worker_feature.tls_churn worker_feature.shutdown_direct worker_feature.shutdown_one worker_feature.shutdown_four
        worker_feature.tls_shutdown_one worker_feature.tls_shutdown_four PROPERTIES TIMEOUT 120)
    xutils_regression(thread_fault_regression sys start mutex fatal task)
    target_link_options(thread_fault_regression PRIVATE -Wl,--wrap=pthread_attr_init -Wl,--wrap=pthread_attr_destroy
        -Wl,--wrap=pthread_attr_setstacksize -Wl,--wrap=pthread_attr_setdetachstate -Wl,--wrap=pthread_create
        -Wl,--wrap=pthread_mutexattr_init -Wl,--wrap=pthread_mutexattr_destroy -Wl,--wrap=pthread_mutexattr_settype
        -Wl,--wrap=pthread_mutex_init -Wl,--wrap=pthread_mutex_lock -Wl,--wrap=pthread_mutex_unlock
        -Wl,--wrap=pthread_mutex_destroy
        -Wl,--wrap=pthread_rwlock_init -Wl,--wrap=pthread_rwlock_rdlock -Wl,--wrap=pthread_rwlock_wrlock
        -Wl,--wrap=pthread_rwlock_unlock -Wl,--wrap=pthread_rwlock_destroy -Wl,--wrap=exit)
    xutils_regression(xfs_fault_regression sys metadata load copy remove types flags)
    target_link_options(xfs_fault_regression PRIVATE -Wl,--wrap=malloc -Wl,--wrap=open -Wl,--wrap=fstat
        -Wl,--wrap=read -Wl,--wrap=write)
    xutils_regression(sys_fault_regression net create nonblock accept api_setup api_timer event_destroy api_destroy
        timer_read read_paths user_actions api_connect ntp_failure worker_rebuild worker_parent worker_limits worker_peak
        worker_flags wait_failure)
    target_link_options(sys_fault_regression PRIVATE -Wl,--wrap=socket -Wl,--wrap=listen -Wl,--wrap=connect
        -Wl,--wrap=setsockopt -Wl,--wrap=fcntl -Wl,--wrap=accept -Wl,--wrap=accept4
        -Wl,--wrap=epoll_create1 -Wl,--wrap=epoll_wait -Wl,--wrap=epoll_ctl -Wl,--wrap=timerfd_create -Wl,--wrap=timerfd_settime
        -Wl,--wrap=read -Wl,--wrap=__read_chk -Wl,--wrap=send -Wl,--wrap=sendto -Wl,--wrap=chmod -Wl,--wrap=calloc
        -Wl,--wrap=free)
    if(OPENSSL_FOUND)
        xutils_regression(rsa_fault_regression crypt generate import replace errors)
        target_link_options(rsa_fault_regression PRIVATE -Wl,--wrap=BN_new -Wl,--wrap=BN_set_word -Wl,--wrap=RSA_new
            -Wl,--wrap=RSA_generate_key_ex -Wl,--wrap=BIO_new -Wl,--wrap=BIO_read -Wl,--wrap=BIO_new_mem_buf
            -Wl,--wrap=PEM_write_bio_RSAPrivateKey -Wl,--wrap=PEM_write_bio_RSAPublicKey -Wl,--wrap=malloc)
        set_tests_properties(rsa_fault_regression.generate rsa_fault_regression.import PROPERTIES TIMEOUT 300)
        xutils_regression(tls_fault_regression net context identity certificates accept)
        target_link_options(tls_fault_regression PRIVATE -Wl,--wrap=TLS_client_method -Wl,--wrap=TLS_server_method
            -Wl,--wrap=SSL_CTX_new -Wl,--wrap=SSL_new -Wl,--wrap=SSL_CTX_free -Wl,--wrap=SSL_free -Wl,--wrap=SSL_ctrl
            -Wl,--wrap=X509_VERIFY_PARAM_set1_host -Wl,--wrap=X509_VERIFY_PARAM_set1_ip_asc
            -Wl,--wrap=SSL_CTX_use_certificate -Wl,--wrap=SSL_CTX_use_PrivateKey -Wl,--wrap=SSL_CTX_use_certificate_chain_file)
    endif()
endif()

xutils_regression(jwt_regression data optional_lengths api_guards roundtrip refused_key wrong_secret tampering boundaries
    malformed algorithms segments rs256)

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

xutils_regression(event_regression net api_guards error_events lifecycle modify callback_delete timers timer_order wakeup
    status_strings capacity lookup event_fd service_guards timer_lifecycle interrupted write_byte detached_delete urgent_data
    descriptor_limit)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(event_regression PRIVATE -Wl,--wrap=sysconf)
    target_compile_definitions(event_regression PRIVATE XTEST_WRAP_SYSCONF)
endif()
xutils_regression(ws_regression net api_guards lengths partial coalesced invalid_wire creation_guards frame_types
    masking control_frames extra_data allocation mask_lengths append_frame mask_keys)
xutils_regression(http_regression net api_guards partial pipeline headers basic_auth long_basic_auth reuse empty_body
    assemble_empty_body code_strings content_length start_line assemble header_lookup limits callbacks lifecycle unix_and_auth
    many_header_lines header_delimiters start_line_colon long_header_line header_whitespace parse_restores_buffer)

xutils_regression(map_regression data api_guards lifecycle reference_model operations iteration growth hashing
    pair_transfer)
xutils_regression(array_regression data api_guards lifecycle reference_model search_boundaries sorting mutation
    fixed_capacity search_agreement heap_array release_guards pool_clear_reuse sort_scaling sort_unsigned_keys
    insert_holes null_and_bounds search_holes)
xutils_regression(json_regression data api_guards parse_matrix parse_boundaries builders_and_writers generated_string_edges
    byte_boundaries error_states pool_and_stress root_array_and_api_boundaries empty_objects pair_names scan_flat number_text
    escape_reference writer_tokens refused_adds strict_documents error_paths)
xutils_regression(json_limits_regression data boundaries escape_nul pool_dump)
xutils_regression(str_buf_regression data boundaries)
xutils_regression(base64_regression crypt boundaries)
xutils_regression(sha_regression crypt sha256_vectors sha256_streaming sha1_vectors md5_vectors crc32_vectors hmac_vectors)
if(OPENSSL_FOUND)
    xutils_regression(digest_regression crypt boundaries sha256_raw_final hmac_variants)
endif()
if(UNIX)
    xutils_regression(http_flow_feature feature api_crud event_crud socket_crud
        reject_status reject_body reject_type reject_id reject_request)
    xutils_regression(network_feature feature api_http api_ws api_ws_fragments ws_controls ws_reject_status ws_reject_connection
        ws_reject_request api_mdtp api_socket
        event_http event_ws event_mdtp event_socket socket_http socket_ws socket_mdtp socket_stream
        http_client http_short_exchange http_short_perform one_byte without_hash api_tls event_tls socket_tls concurrent
        concurrent_tls reject_request_body reject_response_body reject_request_order reject_response_order reject_request_type
        reject_response_type)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_compile_definitions(network_feature PRIVATE FEATURE_WRAP_SEND)
        target_link_options(network_feature PRIVATE -Wl,--wrap=send -Wl,--wrap=write)
    endif()
    get_property(feature_cases DIRECTORY PROPERTY TESTS)
    foreach(feature_case IN LISTS feature_cases)
        if(feature_case MATCHES "^network_feature\\.")
            set_tests_properties(${feature_case} PROPERTIES TIMEOUT 120)
        endif()
    endforeach()
    set_tests_properties(network_feature.concurrent network_feature.concurrent_tls PROPERTIES TIMEOUT 300)
    xutils_regression(http_client_regression net client_paths connect link_exchange easy_exchange perform
        easy_perform auth_and_status client_failures truncated_answer stream_callback buffered_stream stopped_stream
    terminated_stream content_limit callback_guards length_stream length_buffered
    length_stopped length_terminated length_limit length_truncated)
    xutils_regression(sock_ext_regression net unix_stream force_long_path chunked_transfer nonblocking_accept
        options buffers_and_alloc datagram_options address_accessors flag_guards)
    xutils_regression(sock_timeout_regression net boundaries)
    xutils_regression(sock_api_regression net stream_errors call_guards address_conversion address_info status_strings
        accessors buffers listener create_guards udp failed_create_state address_port_range option_failures
        connect_refused forced_bind_failure)
endif()

xutils_regression(hash_regression data api_guards collisions ownership operations distribution pairs)
xutils_regression(list_regression data api_guards links rings stack_head push removal single_node insert_nodes ring_walk)
xutils_regression(api_io_regression net partial_io callback_disconnect peer_eof peer_eof_forked websocket_fragments
    unexpected_continuation
    buffered_upgrade websocket_burst websocket_burst_invalid eof_with_data read_chunk handshake_trickle client_handshake
    client_handshake_edges client_upgrade_validation server_upgrade_validation http_trickle websocket_control
    websocket_sequence_errors websocket_limits
    websocket_tls_hello
    real_ip http_endless handshake_body_limit mdtp_limit crossed_events reloop buffer_reuse)
xutils_regression(api_http_regression net http_partial http_pipeline http_pipeline_burst auth_key auth_basic auth_short)
if(UNIX)
    xutils_regression(api_branch_regression net timer_actions buffered_actions tick_actions write_actions complete_actions
        ws_key_policy ws_token_refusals http_client_ip cancel_ready)
    xutils_regression(api_ctl_regression net vocabulary init listen_connect endpoint_guards
        timers workers buffers)
    xutils_regression(api_proto_regression net websocket mdtp http_endpoint timers
        status_strings workers worker_affinity)
    xutils_regression(api_events_regression net api_guards event_dispatch peer_reset accept_refused unix_listener
        unknown_type peer_closed peer_hunged user_callback
        interrupted_service tick extend_timer session_guards rx_size http_garbage
        http_oversized mdtp_garbage invalid_role inactive_role endpoint_guards ws_garbage_request
        ws_oversized_request ws_bad_response ws_upgrade_tokens accept_exhausted)
endif()
xutils_regression(buf_regression data api_guards aliasing borrowed pointer_ownership ring_model growth edit
    accessors ownership_transfer heap_buffer data_buffer ring_boundaries
    string_and_stdin read_stdin borrowed_reset)
xutils_regression(str_regression data capacities dynamic split_replace)
xutils_regression(xstring_regression data guards build edit transform substrings tokens construction colors borrowed_copy
    borrowed_resize)
xutils_regression(str_ops_regression data argument_guards split split_scaling match_pathological bounded_copies match compare
    search replace cut tokens casing fill_and_trim duplicate concat colors random glob field_formatting append_remaining
    ansi_widths format_boundaries)
xutils_regression(aes_regression crypt api_guards cbc_padding xbc_carried_iv ecb_vector modes siv_tampering generated_iv
    malformed_ciphertext unpredictable_random fips_vectors siv_vectors block_roundtrip xbc_prefix length_overflow)
if(OPENSSL_FOUND AND OPENSSL_VERSION VERSION_GREATER_EQUAL "3.0")
    xutils_regression(aes_siv_regression crypt openssl_vectors)
endif()
xutils_regression(pool_regression sys alignment_growth reallocation overflow accounting exhaustion heap_pool)
xutils_regression(thread_regression sys contention recursive invalid_start)
xutils_regression(sync_regression sys mutex recursive rwlock barrier task task_start_state sleep atomics misuse)
xutils_regression(time_regression sys calendar serialization http_year formats parse_guards
    conversions differences normalize clock fraction_bounds)
xutils_regression(crypt_regression crypt api_guards keyed_dispatch aes_key_lengths hex transforms dispatch cipher_names
    keyless_roundtrip keyed_roundtrip digests casear multy hex_columns hex_decode_agreement hex_scaling)
if(OPENSSL_FOUND)
    xutils_regression(rsa_regression crypt call_guards signatures encryption invalid_keys key_sizes cross_key
        key_import key_files context size_limits build_support short_ciphertext
        length_overflow key_length_overflow partial_key_files)

    # RSA key generation searches for primes, so how long it takes is
    # probabilistic and on a shared runner the tail is long. These cases are
    # fast in the common case; the limit is there for the tail, not the mean.
    foreach(rsa_case call_guards signatures encryption invalid_keys key_sizes cross_key
            key_import key_files context size_limits build_support short_ciphertext
            length_overflow key_length_overflow partial_key_files)
        set_tests_properties("rsa_regression.${rsa_case}" PROPERTIES TIMEOUT 300)
    endforeach()
endif()
if(UNIX)
    xutils_regression(xfs_regression sys copy permissions directories nonregular_source copy_failures)
    xutils_regression(xfs_ops_regression sys api_guards file_io handles lines load paths directories ownership path_bounds
        short_write read_bounds remove_large_dir long_directories)
    xutils_regression(srch_regression sys interruptions deep_tree name_matching recursion criteria text_search callback entries
        guards text_shapes line_bounds link_entries nocase_binary stdin_search empty_stdin binary_lines long_paths)
    xutils_regression(cli_regression sys async_input window_size window_lines window_align window_display
        progress_bounds progress_output progress_animation input terminal_modes terminal_input read_bounds)
    xutils_regression(sig_regression sys register exit_handler errexit backtrace daemonize)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        xutils_regression(cpu_regression sys count affinity guards)
        xutils_regression(cpu_fallback_regression sys count recovery)
        target_link_options(cpu_fallback_regression PRIVATE -Wl,--wrap=sysconf -Wl,--wrap=XPath_LoadBuffer)
        xutils_regression(mon_regression sys lifecycle sampling restart target_pid)
        xutils_regression(mon_fixture_regression sys temperature_labels temperature_names temperature_bounds
            sensor_index cpu_counters cpu_wide process_names cpu_hotplug cpu_sparse cpu_large load_invalid
            cpu_faults cpu_read_error cpu_concurrent network_rates network_members network_large snapshot_io memory)
        target_link_options(mon_fixture_regression PRIVATE -Wl,--wrap=open -Wl,--wrap=opendir
            -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=read)
    endif()
    xutils_regression(worker_sock_regression net listener_inheritance)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME worker_sock_regression.unix_backlog COMMAND worker_sock_regression unix_backlog)
        set_tests_properties(worker_sock_regression.unix_backlog PROPERTIES LABELS "net;worker_sock_regression" TIMEOUT 15)
    endif()
endif()

if(UNIX)
    xutils_regression(ntp_regression net request_reply no_reply zero_epoch guards failure_closes_socket)
endif()
xutils_regression(rtp_regression net header_fields header_guards csrc packet_blocks packet_guards assemble timestamp)
xutils_regression(mdtp_regression net type_mapping assemble_parse parse_guards payload_size_lies
    header_fields create_guards lifecycle hostile_header parse_into_stale
    header_combinations header_refusals)
xutils_regression(sock_regression net retry_read retry_write empty_and_eof keep_open interrupted_io accept_cloexec
    accept_flags empty_datagram closed_write pipe_write)
xutils_regression(log_regression sys api_paths filtering large_message severities flag_mask config
    formatting file_output rotation throw callback_veto enabled)
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
    xutils_regression(allocation_regression allocation json_cleanup http_header buffer_preservation endpoint_ownership
        mdtp_assemble map_rehash_failure http_parse_failure format_failure search_append_failure ws_fragment_failure
        borrowed_string_failure split_failure long_token_failure tls_context_failure api_response_failure mdtp_header_failure
        http_copy_failure ws_request_failure ws_answer_failure api_accept_failure array_slots_failure)
    target_link_options(allocation_regression PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
endif()

set_tests_properties(api_io_regression.partial_io PROPERTIES TIMEOUT 120)
if(OPENSSL_FOUND AND OPENSSL_VERSION VERSION_GREATER_EQUAL "1.1.0")
    xutils_regression(tls_regression net pending_record wrong_hostname untrusted)
    if(UNIX)
        xutils_regression(sock_tls_regression net handshake nonblocking_pair pkcs12_chain stale_error_queue pkcs12_identity
            pkcs12_ownership untrusted cert_guards peer_endings closed_session_io status_strings write_after_reset plain_calls
            ssl_teardown sigpipe_ignored)

        # sigpipe_ignored counts the signal mask changes the TLS guard makes, through the linker's --wrap
        if(NOT APPLE)
            target_link_options(sock_tls_regression PRIVATE -Wl,--wrap=pthread_sigmask)
            target_compile_definitions(sock_tls_regression PRIVATE XTEST_WRAP_SIGMASK)
        endif()
        xutils_regression(api_tls_regression net handshake async_client async_stalled pkcs12_listener partial_writes
            plaintext_client missing_identity)

        # Every case here runs a real handshake and moves bytes through it, so
        # under valgrind they are an order of magnitude slower than the rest.
        foreach(tls_case handshake async_client async_stalled pkcs12_listener partial_writes plaintext_client missing_identity)
            set_tests_properties("api_tls_regression.${tls_case}" PROPERTIES TIMEOUT 300)
        endforeach()

        foreach(sock_tls_case handshake nonblocking_pair pkcs12_chain pkcs12_identity pkcs12_ownership untrusted cert_guards
                peer_endings closed_session_io status_strings write_after_reset plain_calls ssl_teardown sigpipe_ignored)
            set_tests_properties("sock_tls_regression.${sock_tls_case}" PROPERTIES TIMEOUT 300)
        endforeach()
    endif()
endif()
