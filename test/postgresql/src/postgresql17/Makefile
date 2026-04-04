# Makefile for pgagroal PostgreSQL 17 test container

IMAGE_NAME = pgagroal-test-postgresql17-rocky10
CONTAINER_NAME = pgagroal-test-postgresql17

# Detect container engine
CONTAINER_ENGINE := $(shell \
	if command -v podman >/dev/null 2>&1; then echo podman; \
	elif command -v docker >/dev/null 2>&1; then \
		if [ "$$(id -u)" -eq 0 ]; then echo docker; else echo "sudo docker"; fi; \
	else echo "Neither Docker nor Podman is installed" >&2; exit 1; \
	fi)

.PHONY: all build clean run stop

all: build

build:
	@echo "Building PostgreSQL 17 container image: $(IMAGE_NAME)"
	$(CONTAINER_ENGINE) build -t $(IMAGE_NAME) -f Dockerfile .

clean:
	@echo "Cleaning PostgreSQL 17 container image: $(IMAGE_NAME)"
	$(CONTAINER_ENGINE) rmi $(IMAGE_NAME) 2>/dev/null || true

run:
	@echo "Running PostgreSQL 17 container: $(CONTAINER_NAME)"
	$(CONTAINER_ENGINE) run -p 5432:5432 --name $(CONTAINER_NAME) -d \
		-e PG_DATABASE=mydb \
		-e PG_USER_NAME=myuser \
		-e PG_USER_PASSWORD=password \
		-e PG_REPL_USER_NAME=repl \
		-e PG_REPL_PASSWORD=password \
		-e PG_LOG_LEVEL=debug5 \
		$(IMAGE_NAME)

stop:
	@echo "Stopping PostgreSQL 17 container: $(CONTAINER_NAME)"
	$(CONTAINER_ENGINE) stop $(CONTAINER_NAME) 2>/dev/null || true
	$(CONTAINER_ENGINE) rm -f $(CONTAINER_NAME) 2>/dev/null || true