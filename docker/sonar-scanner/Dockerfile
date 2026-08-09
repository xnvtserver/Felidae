FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG SONAR_SCANNER_VERSION=6.2.1.4610

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        clang \
        cmake \
        curl \
        ninja-build \
        openjdk-17-jre-headless \
        unzip \
    && rm -rf /var/lib/apt/lists/* \
    && curl --fail --location --silent --show-error \
        --output /tmp/sonar-scanner.zip \
        "https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-${SONAR_SCANNER_VERSION}-linux-x64.zip" \
    && unzip -q /tmp/sonar-scanner.zip -d /opt \
    && mv "/opt/sonar-scanner-${SONAR_SCANNER_VERSION}-linux-x64" /opt/sonar-scanner \
    && rm /tmp/sonar-scanner.zip

ENV PATH="/opt/sonar-scanner/bin:${PATH}"
