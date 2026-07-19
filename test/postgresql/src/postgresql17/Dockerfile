 # Copyright (C) 2025 The pgagroal community
 #
 # Redistribution and use in source and binary forms, with or without modification,
 # are permitted provided that the following conditions are met:
 #
 # 1. Redistributions of source code must retain the above copyright notice, this list
 # of conditions and the following disclaimer.
 #
 # 2. Redistributions in binary form must reproduce the above copyright notice, this
 # list of conditions and the following disclaimer in the documentation and/or other
 # materials provided with the distribution.
 #
 # 3. Neither the name of the copyright holder nor the names of its contributors may
 # be used to endorse or promote products derived from this software without specific
 # prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 # OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 # THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 # SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 # OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 # HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 # TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 # SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

FROM rockylinux/rockylinux:10

LABEL maintainer="Jesper Pedersen <jesperpedersen.db@gmail.com>"

LABEL summary="PostgreSQL 17" \
      description="PostgreSQL 17"

ENV PGVERSION="17"
ENV PGROOT="/usr/pgsql-${PGVERSION}"

RUN rpm -Uvh https://dl.fedoraproject.org/pub/epel/epel-release-latest-10.noarch.rpm
RUN dnf -y install --nogpgcheck --allowerasing https://download.postgresql.org/pub/repos/yum/reporpms/EL-10-x86_64/pgdg-redhat-repo-latest.noarch.rpm
# The PGDG aarch64 repositories are signed with a separate per-architecture key
# (PGDG-RPM-GPG-KEY-AARCH64-RHEL) that the repo RPM does not install, so on
# aarch64 `dnf makecache` and the postgresql17 install fail with "Signing key
# not found". Import it and add it to gpgkey= so GPG checking stays enabled on
# both architectures (a harmless extra trusted key on x86_64). See #965.
RUN curl -so /etc/pki/rpm-gpg/PGDG-RPM-GPG-KEY-AARCH64-RHEL https://download.postgresql.org/pub/repos/yum/keys/PGDG-RPM-GPG-KEY-AARCH64-RHEL && \
    sed -i 's|^gpgkey=.*|& file:///etc/pki/rpm-gpg/PGDG-RPM-GPG-KEY-AARCH64-RHEL|' /etc/yum.repos.d/pgdg-redhat-all.repo && \
    rpm --import /etc/pki/rpm-gpg/PGDG-RPM-GPG-KEY-AARCH64-RHEL
RUN dnf -qy --nogpgcheck module disable postgresql 2>/dev/null || true
RUN dnf -y upgrade
RUN dnf install -y dnf-plugins-core
RUN dnf config-manager --set-enabled crb
RUN dnf makecache
RUN dnf -y install procps wget iputils net-tools
RUN dnf -y install postgresql17 postgresql17-server postgresql17-contrib postgresql17-libs
RUN dnf --nogpgcheck clean all

WORKDIR /
RUN rm -Rf /conf /pgconf /pgdata /pgwal
COPY root/ /
RUN mkdir -p /conf /pgconf /pgdata /pgwal /pglog
COPY conf/* /conf/
RUN chown -R postgres:postgres /conf /pgconf /pgdata /pgwal /pglog
RUN chmod 700 /conf /pgconf /pgdata /pgwal
RUN chmod +x /usr/bin/run-postgresql
RUN mkdir -p /usr/local/bin

EXPOSE 5432

USER 26

CMD ["/usr/bin/run-postgresql"]