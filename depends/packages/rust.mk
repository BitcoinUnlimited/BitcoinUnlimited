package=rust
$(package)_version=1.45.2
$(package)_download_path=https://static.rust-lang.org/dist
$(package)_file_name=rust-$($(package)_version)-x86_64-unknown-linux-gnu.tar.gz
$(package)_sha256_hash=860feed955726a4d96ffe40758a110053326b9ae11c9e1ee059e9c6222f25643

define $(package)_stage_cmds
  ./install.sh --destdir=$($(package)_staging_dir) --prefix=$(host_prefix)/native --disable-ldconfig
endef
