package=rust
$(package)_version=1.73.0
$(package)_download_path=https://static.rust-lang.org/dist
$(package)_file_name=rust-$($(package)_version)-x86_64-unknown-linux-gnu.tar.gz
$(package)_sha256_hash=aa4cf0b7e66a9f5b7c623d4b340bb1ac2864a5f2c2b981f39f796245dc84f2cb

define $(package)_stage_cmds
  ./install.sh --destdir=$($(package)_staging_dir) --prefix=$(host_prefix)/native --disable-ldconfig
endef
