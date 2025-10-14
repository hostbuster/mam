file(REMOVE_RECURSE
  "libmam_render.a"
  "libmam_render.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/mam_render.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
