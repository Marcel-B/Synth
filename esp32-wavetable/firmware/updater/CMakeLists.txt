# The updater: a small second app in the partition "updater" that opens a Wi-Fi network of its own and writes the
# next image into the instrument's slot. Built once and put into every image by modul.yml:
#   idf.py -B build-updater -C updater build
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tonwerk-updater)
