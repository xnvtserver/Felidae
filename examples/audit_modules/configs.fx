ServerConfig(name: "primary", port: 8080).
ServerConfig(name: "secondary", port: 8081).
ServerConfig(name: "tertiary", port: 8082).

Documentation(name: "primary", doc: "Primary server configuration").
Documentation(name: "secondary", doc: "Secondary server configuration").
Documentation(name: "tertiary", doc: "Tertiary server configuration").

DockerConfig(name: "primary", image: "myapp:latest", ports: [8080, 8081]).
DockerConfig(name: "secondary", image: "myapp:stable", ports: [8081, 8082]).
DockerConfig(name: "tertiary", image: "myapp:stable", ports: [8082, 8083]).
