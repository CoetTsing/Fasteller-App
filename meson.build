project('basicfwd', 'c')
 
dpdk_dep = declare_dependency(
    dependencies: dependency('libdpdk'),
    link_args: [
        '-Wl,--no-as-needed',
        '-L/usr/local/lib64',
        '-lrte_net_mlx5',
        '-lrte_mempool_ring'
    ],
)

hiredis_dep = dependency('hiredis')
 
sources = files(
    'basicfwd.c'
)
 
executable('basicfwd',sources,
    dependencies: [dpdk_dep, hiredis_dep]
)