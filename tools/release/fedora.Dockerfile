FROM fedora:43@sha256:a651ddf48ea28a06ed4e1e6519f51c9f47e7a5a138722ade87369b8fbb7e5b42
RUN dnf install -y gcc-c++ cmake ninja-build rpm-build redhat-rpm-config \
    file desktop-file-utils qt6-qtbase-devel qt6-qtsvg-devel qt6-qtimageformats \
    && dnf clean all
WORKDIR /work
