#!/usr/bin/env python

def submit_experiment(kernel, ts, nx, ny, nz, nt, is_dp, outfile, target_dir, tgs, cs, exe_cmd):
  import os
  import subprocess
  from string import Template
  from utils import ensure_dir    

  job_template=Template(
"""$exec_path --n-tests 2 --disable-source-point --npx 1 --npy 1 --npz 1 --nx $nx --ny $ny --nz $nz  --verbose 1 --target-ts $ts --nt $nt --target-kernel $kernel --cache-size $cs --thread-group-size $tgs | tee $outfile""")

  target_dir = os.path.join(os.path.abspath("."),target_dir)
  ensure_dir(target_dir)
  outpath = os.path.join(target_dir,outfile)

  if(is_dp==1):
    exec_path = os.path.join(os.path.abspath("."),"build_dp/mwd_kernel")
  else:
    exec_path = os.path.join(os.path.abspath("."),"build/mwd_kernel")

  job_cmd = job_template.substitute(cs=cs, tgs=tgs, nx=nx, ny=ny, nz=nz, nt=nt, kernel=kernel, ts=ts, outfile=outpath, exec_path=exec_path, target_dir=target_dir)

  job_cmd = exe_cmd + job_cmd
 
  print job_cmd
  sts = subprocess.call(job_cmd, shell=True)



def ts_test(target_dir, exp_name, cs, ts, kernel, tgs, N, th_l): 
  is_dp = 1
  group = 'DATA'
  for th in th_l:
    if th%tgs == 0:
      exe_cmd = "export OMP_NUM_THREADS=%d; likwid-perfctr -m -C S0:0-%d -s 0x01 -g %s -- " % (th, th-1, group)
      outfile=(exp_name + 'isDP%d_ts%d_kernel%d_tgs%d_N%d_th%d_group_%s.txt' % (is_dp, ts, kernel, tgs,  N, th, group))
      nx = N
      ny = N
      nz = N
      nt = int(max(10 * 4e9/(nx*ny*nz*3), 50))
      submit_experiment(kernel, ts=ts, nx=nx, ny=ny, nz=nz, nt=nt, is_dp=is_dp, 
         outfile=outfile, target_dir=target_dir, tgs=tgs, cs=cs, exe_cmd=exe_cmd)


def main():

  from utils import create_project_tarball 

  import socket
  hostname = socket.gethostname()
  exp_name = "hw14c_"
  exp_name = exp_name + "threadscaling_energy_at_%s" % (hostname)  

  tarball_dir='results/'+exp_name
  create_project_tarball(tarball_dir, "project_"+exp_name)

  target_dir='results/' + exp_name 

  th_l = [1, 2, 4, 6, 8, 10, 12, 14]
  cs = 4096
  #ts_test(target_dir, exp_name[:-9], cs, ts=2, kernel=1, tgs=1, N=960, th_l=th_l[1:]) 
  #ts_test(target_dir, exp_name[:-9], cs, ts=2, kernel=1, tgs=2, N=960, th_l=th_l[4:]) 
  #ts_test(target_dir, exp_name[:-9], cs, ts=2, kernel=1, tgs=7, N=960, th_l=[14]) 
  #for i in th_l:
  #  ts_test(target_dir, exp_name[:-9], cs, ts=2, kernel=1, tgs=i, N=960, th_l=[i]) 



  cs = 8192
  ts_test(target_dir, exp_name[:-9], cs, ts=0, kernel=1, tgs=1, N=960, th_l=th_l) 



if __name__ == "__main__":
  main()
