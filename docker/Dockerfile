FROM python:3.12.14-slim-bookworm

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1
WORKDIR /app

COPY requirements.txt ./
RUN python -m pip install --no-cache-dir --disable-pip-version-check -r requirements.txt
COPY scripts/ ./scripts/
COPY tests/ ./tests/

USER 1000:1000
ENTRYPOINT ["python", "scripts/status.py"]
CMD []
