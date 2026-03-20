{{/*
Expand the name of the chart.
*/}}
{{- define "sep-rbs-server.name" -}}
{{- default .Chart.Name .Values.nameOverride | trunc 63 | trimSuffix "-" }}
{{- end }}

{{/*
Create a default fully qualified app name.
*/}}
{{- define "sep-rbs-server.fullname" -}}
{{- if .Values.fullnameOverride }}
{{- .Values.fullnameOverride | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- $name := default .Chart.Name .Values.nameOverride }}
{{- if contains $name .Release.Name }}
{{- .Release.Name | trunc 63 | trimSuffix "-" }}
{{- else }}
{{- printf "%s-%s" .Release.Name $name | trunc 63 | trimSuffix "-" }}
{{- end }}
{{- end }}
{{- end }}

{{/*
Chart label.
*/}}
{{- define "sep-rbs-server.chart" -}}
{{ .Chart.Name }}-{{ .Chart.Version | replace "+" "_" }}
{{- end }}

{{/*
Common labels.
*/}}
{{- define "sep-rbs-server.labels" -}}
helm.sh/chart: {{ include "sep-rbs-server.chart" . }}
{{ include "sep-rbs-server.selectorLabels" . }}
app.kubernetes.io/version: {{ .Chart.AppVersion | quote }}
app.kubernetes.io/managed-by: {{ .Release.Service }}
{{- end }}

{{/*
Selector labels.
*/}}
{{- define "sep-rbs-server.selectorLabels" -}}
app.kubernetes.io/name: {{ include "sep-rbs-server.name" . }}
app.kubernetes.io/instance: {{ .Release.Name }}
{{- end }}

{{/*
Extract hostname from a public URL for ingress rules.
Examples:
- http://example.com/      -> example.com
- https://example.com/app  -> example.com
- example.com              -> example.com
*/}}
{{- define "sep-rbs-server.ingressHost" -}}
{{- $publicUrl := default "" .Values.ingress.publicUrl | trim -}}
{{- $withoutScheme := regexReplaceAll "^https?://" $publicUrl "" -}}
{{- $withoutPath := regexReplaceAll "/.*$" $withoutScheme "" -}}
{{- trimSuffix "/" $withoutPath -}}
{{- end }}
