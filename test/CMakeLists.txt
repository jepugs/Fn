# needed because of bug where CMAKE_CXX_COMPILER_VERSION isn't set
set(Boost_NO_BOOST_CMAKE ON)

find_package(Boost COMPONENTS unit_test_framework)

function(add_fn_test SOURCE)
  # set the filename to the stem of the file
  get_filename_component(TEST_NAME "${SOURCE}" NAME_WE)
  add_executable("test_${TEST_NAME}" "${SOURCE}")
  target_link_libraries("test_${TEST_NAME}" PRIVATE fn_lib Boost::unit_test_framework)
  add_test(NAME "test_${TEST_NAME}"
           COMMAND "test_${TEST_NAME}" --log_level=message)
endfunction()

if (BOOST_FOUND)
  # build tests
  add_fn_test("scan.test.cpp")
else()
  message("Boost not found. Skipping building tests.")
endif()

