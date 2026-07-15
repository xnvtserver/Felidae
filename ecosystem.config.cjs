module.exports = {
  apps: [
    {
      name: "felidae-docs",
      cwd: "/home/vivek/Desktop/Vivek/WORKSPACE/Felidae",
      script: "./build/felidae.exe",
      args: "docs/server.fx",
      interpreter: "none",
      exec_mode: "fork",
      instances: 1,
      autorestart: true,
      watch: false,
      max_restarts: 10,
      restart_delay: 3000,
      kill_timeout: 5000,
      time: true,
      out_file: "./logs/felidae-docs.out.log",
      error_file: "./logs/felidae-docs.err.log",
      merge_logs: true,
      env: {
        NODE_ENV: "production",
      },
    },
  ],
};
