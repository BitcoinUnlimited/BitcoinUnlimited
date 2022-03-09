package=rust
$(package)_version=1.59.0
$(package)_download_path=https://static.rust-lang.org/dist
$(package)_file_name=rust-$($(package)_version)-x86_64-unknown-linux-gnu.tar.gz
$(package)_sha256_hash=0c1c2da3fa26372e5178123aa5bb0fdcd4933fbad9bfb268ffbd71807182ecae

define $(package)_stage_cmds
  ./install.sh --destdir=$($(package)_staging_dir) --prefix=$(host_prefix)/native --disable-ldconfig
endef
