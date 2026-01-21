Name:          pgagroal
Version:       2.0.0
Release:       1%{dist}
Summary:       High-performance connection pool for PostgreSQL
License:       BSD
URL:           https://github.com/pgagroal/pgagroal
Source0:       %{name}-%{version}.tar.gz

BuildRequires: gcc cmake make python3-docutils
BuildRequires: liburing liburing-devel openssl openssl-devel systemd systemd-devel libatomic zlib zlib-devel libzstd libzstd-devel lz4 lz4-devel bzip2 bzip2-devel binutils
Requires:      liburing openssl systemd libatomic zlib libzstd lz4 bzip2 binutils

%description
pgagroal is a high-performance connection pool for PostgreSQL.

%prep
%setup -q

%build

%{__mkdir} build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DDOCS=OFF ..
%{__make}

%install

%{__mkdir} -p %{buildroot}%{_sysconfdir}
%{__mkdir} -p %{buildroot}%{_bindir}
%{__mkdir} -p %{buildroot}%{_libdir}
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/grafana
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/etc
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/images
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/shell_comp
%{__mkdir} -p %{buildroot}%{_docdir}/%{name}/manual/en
%{__mkdir} -p %{buildroot}%{_mandir}/man1
%{__mkdir} -p %{buildroot}%{_mandir}/man5
%{__mkdir} -p %{buildroot}%{_sysconfdir}/pgagroal

%{__install} -m 644 %{_builddir}/%{name}-%{version}/LICENSE %{buildroot}%{_docdir}/%{name}/LICENSE
%{__install} -m 644 %{_builddir}/%{name}-%{version}/CODE_OF_CONDUCT.md %{buildroot}%{_docdir}/%{name}/CODE_OF_CONDUCT.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/README.md %{buildroot}%{_docdir}/%{name}/README.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/dashboard.json %{buildroot}%{_docdir}/%{name}/grafana/dashboard.json
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/grafana/README.md %{buildroot}%{_docdir}/%{name}/grafana/README.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/ARCHITECTURE.md %{buildroot}%{_docdir}/%{name}/ARCHITECTURE.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/CLI.md %{buildroot}%{_docdir}/%{name}/CLI.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/CONFIGURATION.md %{buildroot}%{_docdir}/%{name}/CONFIGURATION.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/VAULT.md %{buildroot}%{_docdir}/%{name}/VAULT.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/FAILOVER.md %{buildroot}%{_docdir}/%{name}/FAILOVER.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/GETTING_STARTED.md %{buildroot}%{_docdir}/%{name}/GETTING_STARTED.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/PERFORMANCE.md %{buildroot}%{_docdir}/%{name}/PERFORMANCE.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/PIPELINES.md %{buildroot}%{_docdir}/%{name}/PIPELINES.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/RPM.md %{buildroot}%{_docdir}/%{name}/RPM.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/SECURITY.md %{buildroot}%{_docdir}/%{name}/SECURITY.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/DISTRIBUTIONS.md %{buildroot}%{_docdir}/%{name}/DISTRIBUTIONS.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgagroal.service %{buildroot}%{_docdir}/%{name}/etc/pgagroal.service
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgagroal.socket %{buildroot}%{_docdir}/%{name}/etc/pgagroal.socket
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/images/perf-extended.png %{buildroot}%{_docdir}/%{name}/images/perf-extended.png
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/images/perf-prepared.png %{buildroot}%{_docdir}/%{name}/images/perf-prepared.png
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/images/perf-readonly.png %{buildroot}%{_docdir}/%{name}/images/perf-readonly.png
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/images/perf-simple.png %{buildroot}%{_docdir}/%{name}/images/perf-simple.png
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/shell_comp/pgagroal_comp.bash %{buildroot}%{_docdir}/%{name}/shell_comp/pgagroal_comp.bash
%{__install} -m 644 %{_builddir}/%{name}-%{version}/contrib/shell_comp/pgagroal_comp.zsh %{buildroot}%{_docdir}/%{name}/shell_comp/pgagroal_comp.zsh
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/00-frontpage.md %{buildroot}%{_docdir}/%{name}/manual/en/00-frontpage.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/01-introduction.md %{buildroot}%{_docdir}/%{name}/manual/en/01-introduction.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/02-installation.md %{buildroot}%{_docdir}/%{name}/manual/en/02-installation.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/03-gettingstarted.md %{buildroot}%{_docdir}/%{name}/manual/en/03-gettingstarted.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/04-configuration.md %{buildroot}%{_docdir}/%{name}/manual/en/04-configuration.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/05-prefill.md %{buildroot}%{_docdir}/%{name}/manual/en/05-prefill.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/06-remote_management.md %{buildroot}%{_docdir}/%{name}/manual/en/06-remote_management.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/07-split_security.md %{buildroot}%{_docdir}/%{name}/manual/en/07-split_security.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/08-tls.md %{buildroot}%{_docdir}/%{name}/manual/en/08-tls.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/09-database_alias.md %{buildroot}%{_docdir}/%{name}/manual/en/09-database_alias.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/10-vault.md %{buildroot}%{_docdir}/%{name}/manual/en/10-vault.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/11-prometheus.md %{buildroot}%{_docdir}/%{name}/manual/en/11-prometheus.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/12-docker.md %{buildroot}%{_docdir}/%{name}/manual/en/12-docker.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/13-cli-tools.md %{buildroot}%{_docdir}/%{name}/manual/en/13-cli-tools.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/14-performance.md %{buildroot}%{_docdir}/%{name}/manual/en/14-performance.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/15-failover.md %{buildroot}%{_docdir}/%{name}/manual/en/15-failover.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/16-pipelines.md %{buildroot}%{_docdir}/%{name}/manual/en/16-pipelines.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/17-security.md %{buildroot}%{_docdir}/%{name}/manual/en/17-security.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/70-dev.md %{buildroot}%{_docdir}/%{name}/manual/en/70-dev.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/71-git.md %{buildroot}%{_docdir}/%{name}/manual/en/71-git.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/72-architecture.md %{buildroot}%{_docdir}/%{name}/manual/en/72-architecture.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/73-rpm.md %{buildroot}%{_docdir}/%{name}/manual/en/73-rpm.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/74-building.md %{buildroot}%{_docdir}/%{name}/manual/en/74-building.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/75-codecoverage.md %{buildroot}%{_docdir}/%{name}/manual/en/75-codecoverage.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/76-eventloop.md %{buildroot}%{_docdir}/%{name}/manual/en/76-eventloop.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/77-core_api.md %{buildroot}%{_docdir}/%{name}/manual/en/77-core_api.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/78-test.md %{buildroot}%{_docdir}/%{name}/manual/en/78-test.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/79-distributions.md %{buildroot}%{_docdir}/%{name}/manual/en/79-distributions.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/97-acknowledgement.md %{buildroot}%{_docdir}/%{name}/manual/en/97-acknowledgement.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/98-licenses.md %{buildroot}%{_docdir}/%{name}/manual/en/98-licenses.md
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/manual/en/99-references.md %{buildroot}%{_docdir}/%{name}/manual/en/99-references.md

%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgagroal.conf %{buildroot}%{_sysconfdir}/pgagroal/pgagroal.conf
%{__install} -m 644 %{_builddir}/%{name}-%{version}/doc/etc/pgagroal_hba.conf %{buildroot}%{_sysconfdir}/pgagroal/pgagroal_hba.conf

%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal.1 %{buildroot}%{_mandir}/man1/pgagroal.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal-admin.1 %{buildroot}%{_mandir}/man1/pgagroal-admin.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal-cli.1 %{buildroot}%{_mandir}/man1/pgagroal-cli.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal-vault.1 %{buildroot}%{_mandir}/man1/pgagroal-vault.1
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal.conf.5 %{buildroot}%{_mandir}/man5/pgagroal.conf.5
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal_databases.conf.5 %{buildroot}%{_mandir}/man5/pgagroal_databases.conf.5
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal_hba.conf.5 %{buildroot}%{_mandir}/man5/pgagroal_hba.conf.5
%{__install} -m 644 %{_builddir}/%{name}-%{version}/build/doc/pgagroal_vault.conf.5 %{buildroot}%{_mandir}/man5/pgagroal_vault.conf.5

%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgagroal %{buildroot}%{_bindir}/pgagroal
%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgagroal-cli %{buildroot}%{_bindir}/pgagroal-cli
%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgagroal-admin %{buildroot}%{_bindir}/pgagroal-admin
%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/pgagroal-vault %{buildroot}%{_bindir}/pgagroal-vault

%{__install} -m 755 %{_builddir}/%{name}-%{version}/build/src/libpgagroal.so.%{version} %{buildroot}%{_libdir}/libpgagroal.so.%{version}

chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgagroal
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgagroal-cli
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgagroal-admin
chrpath -r %{_libdir} %{buildroot}%{_bindir}/pgagroal-vault

cd %{buildroot}%{_libdir}/
%{__ln_s} -f libpgagroal.so.%{version} libpgagroal.so.2
%{__ln_s} -f libpgagroal.so.2 libpgagroal.so

%files
%license %{_docdir}/%{name}/LICENSE
%{_docdir}/%{name}/ARCHITECTURE.md
%{_docdir}/%{name}/CLI.md
%{_docdir}/%{name}/CODE_OF_CONDUCT.md
%{_docdir}/%{name}/CONFIGURATION.md
%{_docdir}/%{name}/VAULT.md
%{_docdir}/%{name}/FAILOVER.md
%{_docdir}/%{name}/GETTING_STARTED.md
%{_docdir}/%{name}/PERFORMANCE.md
%{_docdir}/%{name}/PIPELINES.md
%{_docdir}/%{name}/README.md
%{_docdir}/%{name}/RPM.md
%{_docdir}/%{name}/SECURITY.md
%{_docdir}/%{name}/DISTRIBUTIONS.md
%{_docdir}/%{name}/grafana/dashboard.json
%{_docdir}/%{name}/grafana/README.md
%{_docdir}/%{name}/etc/pgagroal.service
%{_docdir}/%{name}/etc/pgagroal.socket
%{_docdir}/%{name}/images/perf-extended.png
%{_docdir}/%{name}/images/perf-prepared.png
%{_docdir}/%{name}/images/perf-readonly.png
%{_docdir}/%{name}/images/perf-simple.png
%{_docdir}/%{name}/shell_comp/pgagroal_comp.bash
%{_docdir}/%{name}/shell_comp/pgagroal_comp.zsh
%{_docdir}/%{name}/manual/en/00-frontpage.md
%{_docdir}/%{name}/manual/en/01-introduction.md
%{_docdir}/%{name}/manual/en/02-installation.md
%{_docdir}/%{name}/manual/en/03-gettingstarted.md
%{_docdir}/%{name}/manual/en/04-configuration.md
%{_docdir}/%{name}/manual/en/05-prefill.md
%{_docdir}/%{name}/manual/en/06-remote_management.md
%{_docdir}/%{name}/manual/en/07-split_security.md
%{_docdir}/%{name}/manual/en/08-tls.md
%{_docdir}/%{name}/manual/en/09-database_alias.md
%{_docdir}/%{name}/manual/en/10-vault.md
%{_docdir}/%{name}/manual/en/11-prometheus.md
%{_docdir}/%{name}/manual/en/12-docker.md
%{_docdir}/%{name}/manual/en/13-cli-tools.md
%{_docdir}/%{name}/manual/en/14-performance.md
%{_docdir}/%{name}/manual/en/15-failover.md
%{_docdir}/%{name}/manual/en/16-pipelines.md
%{_docdir}/%{name}/manual/en/17-security.md
%{_docdir}/%{name}/manual/en/70-dev.md
%{_docdir}/%{name}/manual/en/71-git.md
%{_docdir}/%{name}/manual/en/72-architecture.md
%{_docdir}/%{name}/manual/en/73-rpm.md
%{_docdir}/%{name}/manual/en/74-building.md
%{_docdir}/%{name}/manual/en/75-codecoverage.md
%{_docdir}/%{name}/manual/en/76-eventloop.md
%{_docdir}/%{name}/manual/en/77-core_api.md
%{_docdir}/%{name}/manual/en/78-test.md
%{_docdir}/%{name}/manual/en/79-distributions.md
%{_docdir}/%{name}/manual/en/97-acknowledgement.md
%{_docdir}/%{name}/manual/en/98-licenses.md
%{_docdir}/%{name}/manual/en/99-references.md
%{_mandir}/man1/pgagroal.1*
%{_mandir}/man1/pgagroal-admin.1*
%{_mandir}/man1/pgagroal-cli.1*
%{_mandir}/man1/pgagroal-vault.1*
%{_mandir}/man5/pgagroal.conf.5*
%{_mandir}/man5/pgagroal_databases.conf.5*
%{_mandir}/man5/pgagroal_hba.conf.5*
%{_mandir}/man5/pgagroal_vault.conf.5*
%config %{_sysconfdir}/pgagroal/pgagroal.conf
%config %{_sysconfdir}/pgagroal/pgagroal_hba.conf
%{_bindir}/pgagroal
%{_bindir}/pgagroal-cli
%{_bindir}/pgagroal-admin
%{_bindir}/pgagroal-vault
%{_libdir}/libpgagroal.so
%{_libdir}/libpgagroal.so.2
%{_libdir}/libpgagroal.so.%{version}

%changelog
