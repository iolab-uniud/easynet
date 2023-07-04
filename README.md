# EasyNet

The [EasyNet project](https://easy-net.info) (in Italian) aims at improving healthcare practice and equity in various clinical and organizational settings  effectiveness by means of Audit & Feedback (A&F) strategies.

This repository contains the code and (anonymized) data used in the WP2 of the project. Specifically, it addresses the A&F of Emergency Medicine.
This is meant for the future comparability and reproducibility of this research.

The code consists of an Emergency Medical Service Simulator, which can be customized with OSRM routing data (see the following example). Moreover, the data used in the experiments with the simulator is available in the form of CSV files.

## Simulator



## Emergency Data

The folder "Anonymized Instances" contains a set of 45 instances related to emergencies. These instances are provided in both CSV and TXT formats. They represent real-world emergencies that have been anonymized in terms of spatial and temporal information. Despite the anonymization, the temporal pattern (i.e., the average number of emergencies per day and per hour) and the spatial information (i.e., preserving the zone within the municipality) have been retained.

Each instance includes emergency data for a one-week time period.

The fields within each instance are as follows:

- municipality_code: This field serves as a unique identifier for the municipality (ISTAT code).
- transport_code: This field serves as a unique identifier for the emergency.
- municipality: This field contains the name of the municipality.
- urgency_code: This field represents the level of urgency of the emergency, classified as red, yellow, green, or white based on the Italian classification system.
- lat: This field specifies the latitude at which the emergency occurred.
- lon: This field specifies the longitude at which the emergency occurred.
- date_time: This field indicates the timestamp of the emergency.
- destination hospital: This field indicates the type of hospital where the emergency should be treated, classified as K: children's hospital, S: spoke hospital, or H: hub hospital.- 